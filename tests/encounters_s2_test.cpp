// S2.01 tier-2 tests: the Act-2 (TheCity) and Act-3 (TheBeyond) encounter rows
// and the per-act pool tables codegen emits for them.
//
// Every expected value below is hand-carried from the decompiled source read in
// full for this task -- TheCity.java:87-182, TheBeyond.java:84-176 and
// MonsterHelper.java:461-594 plus the spawn helpers (spawnGremlin 767-778,
// spawnShapes 664-692, getAncientShape 636-646) -- and NOT read back from the
// generator. The pool tests pin membership, ArrayList add() ORDER (which is the
// tie order MonsterInfo.normalizeWeights' stable sort depends on, TRAP 1),
// weights and exclusion sets; the composition tests are DIFFERENTIAL, replaying
// the exact miscRng draw sequence by hand and requiring both the spawn order and
// the number of draws consumed to match.
//
// The Act-1 group is pinned here too: the act extension is append-only for the
// emitted tables, so the Exordium pools must project out of the new per-act
// tables exactly as B3.12 left them.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "sts/engine/encounters.hpp"
#include "sts/engine/rng_stream.hpp"

using namespace sts::engine;
using sts::registry::EncounterPool;
using sv = std::string_view;

namespace {

struct PoolRow {
    sv key;
    float weight;
};

// The (act, pool) group as codegen emitted it, in add() order.
std::vector<PoolRow> emitted_pool(int32_t act, EncounterPool pool) {
    const auto* t = sts::registry::encounter_pool_table(act, pool);
    if (t == nullptr) {
        return {};
    }
    std::vector<PoolRow> out;
    for (std::size_t i = 0; i < t->count; ++i) {
        out.push_back(PoolRow{t->keys[i], t->weights[i]});
    }
    return out;
}

void expect_pool(int32_t act, EncounterPool pool,
                 const std::vector<PoolRow>& expected, const char* what) {
    const std::vector<PoolRow> got = emitted_pool(act, pool);
    ASSERT_EQ(got.size(), expected.size()) << what;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(got[i].key, expected[i].key) << what << " position " << i;
        EXPECT_FLOAT_EQ(got[i].weight, expected[i].weight)
            << what << " weight of " << expected[i].key;
    }
    // The flat kEncounters array must agree with the group table row for row.
    std::vector<PoolRow> flat;
    for (const auto& e : sts::registry::kEncounters) {
        if (e.act == act && e.pool == pool) {
            flat.push_back(PoolRow{e.game_id, e.weight});
        }
    }
    ASSERT_EQ(flat.size(), expected.size()) << what << " (flat array)";
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(flat[i].key, expected[i].key) << what << " flat position " << i;
    }
}

// The row that owns a (game_id, act, pool) triple -- needed because "3 Darklings"
// has two rows and encounter_by_game_id returns only the first.
const sts::registry::EncounterDef* row_of(sv key, int32_t act,
                                          EncounterPool pool) {
    for (const auto& e : sts::registry::kEncounters) {
        if (e.game_id == key && e.act == act && e.pool == pool) {
            return &e;
        }
    }
    return nullptr;
}

std::vector<sv> exclusions_of(sv key, int32_t act) {
    const auto* e = row_of(key, act, EncounterPool::WEAK);
    if (e == nullptr) {
        return {sv("<missing row>")};
    }
    return std::vector<sv>(e->excludes.begin(),
                           e->excludes.begin() + e->exclude_count);
}

std::vector<sv> members(const ResolvedGroup& g) {
    return std::vector<sv>(g.members.begin(), g.members.begin() + g.count);
}

ResolvedGroup resolve_at(sv key, int64_t seed, int32_t& draws) {
    RngStream misc = from_seed(seed);
    ResolvedGroup g{};
    EXPECT_TRUE(resolve_encounter(key, misc, g)) << key;
    draws = misc.counter;
    return g;
}

// Draw-without-replacement reference: random(pool.size()-1) then
// ArrayList.remove(index) (MonsterHelper.spawnShapes, :673-689).
std::vector<sv> pool_reference(RngStream& ref, std::vector<sv> pool, int n) {
    std::vector<sv> out;
    for (int k = 0; k < n; ++k) {
        const int32_t idx = random(ref, static_cast<int32_t>(pool.size()) - 1);
        out.push_back(pool[static_cast<std::size_t>(idx)]);
        pool.erase(pool.begin() + idx);
    }
    return out;
}

// The 6-slot shape pool in add() order (MonsterHelper.java:665-671).
const std::vector<sv> kShapePool = {"Repulsor", "Repulsor", "Exploder",
                                    "Exploder", "Spiker",   "Spiker"};
// The 8-slot gremlin pool in add() order (MonsterHelper.java:768-776).
const std::vector<sv> kGremlinPool = {"GremlinWarrior",  "GremlinWarrior",
                                      "GremlinThief",    "GremlinThief",
                                      "GremlinFat",      "GremlinFat",
                                      "GremlinTsundere", "GremlinWizard"};

}  // namespace

// --- 1. Per-act pool tables -------------------------------------------------

// TheCity.generateWeakEnemies/generateStrongEnemies/generateElites/initializeBoss
// (TheCity.java:94-130, 153-182), read in full.
TEST(EncountersS2, ActTwoPoolsMatchTheCity) {
    expect_pool(2, EncounterPool::WEAK,
                {{"Spheric Guardian", 2.0f},
                 {"Chosen", 2.0f},
                 {"Shell Parasite", 2.0f},
                 {"3 Byrds", 2.0f},
                 {"2 Thieves", 2.0f}},
                "TheCity weak pool");
    expect_pool(2, EncounterPool::STRONG,
                {{"Chosen and Byrds", 2.0f},
                 {"Sentry and Sphere", 2.0f},
                 {"Snake Plant", 6.0f},
                 {"Snecko", 4.0f},
                 {"Centurion and Healer", 6.0f},
                 {"Cultist and Chosen", 3.0f},
                 {"3 Cultists", 3.0f},
                 {"Shelled Parasite and Fungi", 3.0f}},
                "TheCity strong pool");
    expect_pool(2, EncounterPool::ELITE,
                {{"Gremlin Leader", 1.0f},
                 {"Slavers", 1.0f},
                 {"Book of Stabbing", 1.0f}},
                "TheCity elite pool");
    // initializeBoss's fully-unlocked else-branch order, pre-shuffle.
    expect_pool(2, EncounterPool::BOSS,
                {{"Automaton", 0.0f}, {"Collector", 0.0f}, {"Champ", 0.0f}},
                "TheCity boss list");
    expect_pool(2, EncounterPool::EVENT,
                {{"Masked Bandits", 0.0f},
                 {"Colosseum Nobs", 0.0f},
                 {"Colosseum Slavers", 0.0f}},
                "TheCity event groups");

    // The strong weights sum to 29 -- the denominator normalizeWeights divides by.
    float total = 0.0f;
    for (const auto& r : emitted_pool(2, EncounterPool::STRONG)) {
        total += r.weight;
    }
    EXPECT_FLOAT_EQ(total, 29.0f);
}

// TheBeyond.generateWeakEnemies/generateStrongEnemies/generateElites/
// initializeBoss (TheBeyond.java:91-125, 147-176), read in full.
TEST(EncountersS2, ActThreePoolsMatchTheBeyond) {
    expect_pool(3, EncounterPool::WEAK,
                {{"3 Darklings", 2.0f}, {"Orb Walker", 2.0f}, {"3 Shapes", 2.0f}},
                "TheBeyond weak pool");
    expect_pool(3, EncounterPool::STRONG,
                {{"Spire Growth", 1.0f},
                 {"Transient", 1.0f},
                 {"4 Shapes", 1.0f},
                 {"Maw", 1.0f},
                 {"Sphere and 2 Shapes", 1.0f},
                 {"Jaw Worm Horde", 1.0f},
                 {"3 Darklings", 1.0f},
                 {"Writhing Mass", 1.0f}},
                "TheBeyond strong pool");
    expect_pool(3, EncounterPool::ELITE,
                {{"Giant Head", 2.0f}, {"Nemesis", 2.0f}, {"Reptomancer", 2.0f}},
                "TheBeyond elite pool");
    expect_pool(3, EncounterPool::BOSS,
                {{"Awakened One", 0.0f},
                 {"Time Eater", 0.0f},
                 {"Donu and Deca", 0.0f}},
                "TheBeyond boss list");
    // MysteriousSphere.onEnterRoom enters "2 Orb Walkers"
    // (MysteriousSphere.java:39,87) -- not the same-named encounter key, which
    // has no constructing caller anywhere in the tree.
    expect_pool(3, EncounterPool::EVENT, {{"2 Orb Walkers", 0.0f}},
                "TheBeyond event group");

    // TRAP (s2-design §5.8): "3 Darklings" is in BOTH pools -- two distinct
    // rows, one key, identical programs.
    const auto* weak = row_of("3 Darklings", 3, EncounterPool::WEAK);
    const auto* strong = row_of("3 Darklings", 3, EncounterPool::STRONG);
    ASSERT_NE(weak, nullptr);
    ASSERT_NE(strong, nullptr);
    EXPECT_LT(weak->id, strong->id) << "the weak row must come first: "
                                       "encounter_by_game_id returns it";
    EXPECT_EQ(sts::registry::encounter_by_game_id("3 Darklings"), weak);
    ASSERT_EQ(weak->step_count, strong->step_count);
    for (std::size_t i = 0; i < weak->step_count; ++i) {
        EXPECT_EQ(weak->program[i].refs[0], strong->program[i].refs[0]);
    }
}

// The act extension is additive: the Exordium groups project out of the new
// per-act tables exactly as B3.12 emitted them (same rows, same order, same
// weights). This is the durable form of the one-time before/after header diff.
TEST(EncountersS2, ActOnePoolsUnchangedByTheActExtension) {
    expect_pool(1, EncounterPool::WEAK,
                {{"Cultist", 2.0f},
                 {"Jaw Worm", 2.0f},
                 {"2 Louse", 2.0f},
                 {"Small Slimes", 2.0f}},
                "Exordium weak pool");
    expect_pool(1, EncounterPool::STRONG,
                {{"Blue Slaver", 2.0f},
                 {"Gremlin Gang", 1.0f},
                 {"Looter", 2.0f},
                 {"Large Slime", 2.0f},
                 {"Lots of Slimes", 1.0f},
                 {"Exordium Thugs", 1.5f},
                 {"Exordium Wildlife", 1.5f},
                 {"Red Slaver", 1.0f},
                 {"3 Louse", 2.0f},
                 {"2 Fungi Beasts", 2.0f}},
                "Exordium strong pool");
    expect_pool(1, EncounterPool::ELITE,
                {{"Gremlin Nob", 1.0f}, {"Lagavulin", 1.0f}, {"3 Sentries", 1.0f}},
                "Exordium elite pool");
    expect_pool(1, EncounterPool::BOSS,
                {{"The Guardian", 0.0f},
                 {"Hexaghost", 0.0f},
                 {"Slime Boss", 0.0f}},
                "Exordium boss list");
    expect_pool(1, EncounterPool::EVENT, {{"The Mushroom Lair", 0.0f}},
                "Exordium event group");

    EXPECT_EQ(sts::registry::kEncounterMaxAct, 3);
    // 5 pools x 3 acts, every one populated.
    EXPECT_EQ(sts::registry::kEncounterPoolTableCount, 15);
    for (int32_t act = 1; act <= 3; ++act) {
        for (const auto pool :
             {EncounterPool::WEAK, EncounterPool::STRONG, EncounterPool::ELITE,
              EncounterPool::BOSS, EncounterPool::EVENT}) {
            const auto* t = sts::registry::encounter_pool_table(act, pool);
            ASSERT_NE(t, nullptr) << "act " << act;
            EXPECT_GT(t->count, 0u) << "act " << act;
        }
    }
    EXPECT_EQ(sts::registry::encounter_pool_table(4, EncounterPool::WEAK), nullptr);
}

// --- 2. Exclusion sets ------------------------------------------------------

// TheCity.generateExclusions (TheCity.java:132-151) and
// TheBeyond.generateExclusions (TheBeyond.java:127-145), both read in full: the
// switch has exactly three cases per act, and every other weak key is absent
// (== no exclusions).
TEST(EncountersS2, ExclusionSetsMatchGenerateExclusions) {
    EXPECT_EQ(exclusions_of("Spheric Guardian", 2),
              (std::vector<sv>{"Sentry and Sphere"}));
    EXPECT_EQ(exclusions_of("3 Byrds", 2), (std::vector<sv>{"Chosen and Byrds"}));
    // The only two-key exclusion in the game.
    EXPECT_EQ(exclusions_of("Chosen", 2),
              (std::vector<sv>{"Chosen and Byrds", "Cultist and Chosen"}));
    // Not in the switch -> no exclusions.
    EXPECT_TRUE(exclusions_of("Shell Parasite", 2).empty());
    EXPECT_TRUE(exclusions_of("2 Thieves", 2).empty());

    // Act 3: 3 Darklings self-excludes (and it BITES -- the key is in the strong
    // pool too); Orb Walker self-excludes inertly (weak-only key, so the
    // first-strong rejection loop can never match it); 3 Shapes excludes 4 Shapes.
    EXPECT_EQ(exclusions_of("3 Darklings", 3), (std::vector<sv>{"3 Darklings"}));
    EXPECT_EQ(exclusions_of("Orb Walker", 3), (std::vector<sv>{"Orb Walker"}));
    EXPECT_EQ(exclusions_of("3 Shapes", 3), (std::vector<sv>{"4 Shapes"}));

    const auto strong3 = emitted_pool(3, EncounterPool::STRONG);
    bool darklings_is_strong = false;
    bool orb_walker_is_strong = false;
    for (const auto& r : strong3) {
        darklings_is_strong |= (r.key == sv("3 Darklings"));
        orb_walker_is_strong |= (r.key == sv("Orb Walker"));
    }
    EXPECT_TRUE(darklings_is_strong) << "the self-exclusion must be able to bite";
    EXPECT_FALSE(orb_walker_is_strong) << "the self-exclusion must be inert";

    // Exclusions live on WEAK rows only: the strong "3 Darklings" row carries none.
    const auto* strong_darklings = row_of("3 Darklings", 3, EncounterPool::STRONG);
    ASSERT_NE(strong_darklings, nullptr);
    EXPECT_EQ(strong_darklings->exclude_count, 0u);
}

// --- 3. Compositions with no miscRng draws ----------------------------------

// MonsterHelper.getEncounter (MonsterHelper.java:462-530, 533-593) read in full:
// spawn order == array order == turn order, and refs are AbstractMonster.ID
// strings (Taskmaster.ID == "SlaverBoss", BanditPointy.ID == "BanditChild",
// SpireGrowth.ID == "Serpent", SnakeDagger.ID == "Dagger").
TEST(EncountersS2, FixedActTwoAndThreeCompositionsConsumeNoDraws) {
    struct Case { sv key; std::vector<sv> members; };
    const Case cases[] = {
        // Act 2 weak.
        {"Spheric Guardian", {"SphericGuardian"}},
        {"Chosen", {"Chosen"}},
        {"Shell Parasite", {"Shelled Parasite"}},
        {"3 Byrds", {"Byrd", "Byrd", "Byrd"}},
        {"2 Thieves", {"Looter", "Mugger"}},
        // Act 2 strong.
        {"Chosen and Byrds", {"Byrd", "Chosen"}},
        {"Sentry and Sphere", {"Sentry", "SphericGuardian"}},
        {"Snake Plant", {"SnakePlant"}},
        {"Snecko", {"Snecko"}},
        {"Centurion and Healer", {"Centurion", "Healer"}},
        {"Cultist and Chosen", {"Cultist", "Chosen"}},
        {"3 Cultists", {"Cultist", "Cultist", "Cultist"}},
        {"Shelled Parasite and Fungi", {"Shelled Parasite", "FungiBeast"}},
        // Act 2 elite + boss + event.
        {"Slavers", {"SlaverBlue", "SlaverBoss", "SlaverRed"}},
        {"Book of Stabbing", {"BookOfStabbing"}},
        {"Automaton", {"BronzeAutomaton"}},
        {"Collector", {"TheCollector"}},
        {"Champ", {"Champ"}},
        {"Masked Bandits", {"BanditChild", "BanditLeader", "BanditBear"}},
        {"Colosseum Nobs", {"SlaverBoss", "GremlinNob"}},
        {"Colosseum Slavers", {"SlaverBlue", "SlaverRed"}},
        // Act 3.
        {"3 Darklings", {"Darkling", "Darkling", "Darkling"}},
        {"Orb Walker", {"Orb Walker"}},
        {"Spire Growth", {"Serpent"}},
        {"Transient", {"Transient"}},
        {"Maw", {"Maw"}},
        {"Jaw Worm Horde", {"JawWorm", "JawWorm", "JawWorm"}},
        {"Writhing Mass", {"WrithingMass"}},
        {"Giant Head", {"GiantHead"}},
        {"Nemesis", {"Nemesis"}},
        // Dagger, Reptomancer, dagger -- POSX[1] before POSX[0].
        {"Reptomancer", {"Dagger", "Reptomancer", "Dagger"}},
        {"Awakened One", {"Cultist", "Cultist", "AwakenedOne"}},
        {"Time Eater", {"TimeEater"}},
        // Deca FIRST, despite the key's name.
        {"Donu and Deca", {"Deca", "Donu"}},
        {"2 Orb Walkers", {"Orb Walker", "Orb Walker"}},
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

// --- 4. Compositions that consume miscRng -----------------------------------

// Gremlin Leader: spawnGremlin is called TWICE, and each call rebuilds the
// 8-slot pool before drawing miscRng.random(0, size-1) -- so the two minions are
// drawn WITH replacement (MonsterHelper.java:508, 767-778). random(0,7) and
// random(7) are the same draw (Random.java:53-61, both nextInt(8)).
TEST(EncountersS2, GremlinLeaderDrawsTwoMinionsWithReplacement) {
    bool saw_duplicate_pair = false;
    for (int64_t seed = 0; seed < 200; ++seed) {
        RngStream ref = from_seed(seed);
        std::vector<sv> exp;
        for (int i = 0; i < 2; ++i) {
            const int32_t idx = random(ref, 0, 7);
            exp.push_back(kGremlinPool[static_cast<std::size_t>(idx)]);
        }
        exp.push_back("GremlinLeader");

        int32_t draws = -1;
        const ResolvedGroup g = resolve_at("Gremlin Leader", seed, draws);
        EXPECT_EQ(members(g), exp) << "seed=" << seed;
        EXPECT_EQ(draws, 2) << "seed=" << seed;
        EXPECT_EQ(ref.counter, 2);
        if (exp[0] == exp[1]) {
            saw_duplicate_pair = true;
        }
    }
    EXPECT_TRUE(saw_duplicate_pair)
        << "with replacement, identical minions must be reachable";
}

// 3 Shapes / 4 Shapes: spawnShapes draws WITHOUT replacement from the same
// 6-slot pool, 3 times when weak and 4 when not (MonsterHelper.java:664-692).
TEST(EncountersS2, ShapesDrawWithoutReplacementFromTheSharedPool) {
    for (int64_t seed = 0; seed < 60; ++seed) {
        for (const int n : {3, 4}) {
            RngStream ref = from_seed(seed);
            const std::vector<sv> exp = pool_reference(ref, kShapePool, n);
            int32_t draws = -1;
            const ResolvedGroup g =
                resolve_at(n == 3 ? sv("3 Shapes") : sv("4 Shapes"), seed, draws);
            EXPECT_EQ(members(g), exp) << "n=" << n << " seed=" << seed;
            EXPECT_EQ(draws, n) << "n=" << n << " seed=" << seed;
            EXPECT_EQ(ref.counter, n);
        }
    }
    // Same pool, same prefix: the first three draws of "4 Shapes" are exactly
    // "3 Shapes" at the same seed -- the fourth draw is the only difference.
    for (int64_t seed = 0; seed < 20; ++seed) {
        int32_t d3 = -1;
        int32_t d4 = -1;
        const ResolvedGroup g3 = resolve_at("3 Shapes", seed, d3);
        const ResolvedGroup g4 = resolve_at("4 Shapes", seed, d4);
        ASSERT_EQ(g3.count, 3u);
        ASSERT_EQ(g4.count, 4u);
        for (std::size_t i = 0; i < 3; ++i) {
            EXPECT_EQ(g3.members[i], g4.members[i]) << "seed=" << seed;
        }
    }
}

// Sphere and 2 Shapes: getAncientShape twice, each ONE miscRng.random(2) mapping
// 0 -> Spiker, 1 -> Repulsor, default (2) -> Exploder, and constructing only the
// selected monster (MonsterHelper.java:568, 636-646). Then a SphericGuardian.
TEST(EncountersS2, AncientShapesAreOneDrawEachAndConstructOnlyTheChoice) {
    const sv kByIndex[3] = {"Spiker", "Repulsor", "Exploder"};
    bool seen[3] = {false, false, false};
    for (int64_t seed = 0; seed < 60; ++seed) {
        RngStream ref = from_seed(seed);
        std::vector<sv> exp;
        for (int i = 0; i < 2; ++i) {
            const int32_t r = random(ref, 2);
            ASSERT_GE(r, 0);
            ASSERT_LE(r, 2);
            seen[r] = true;
            exp.push_back(kByIndex[r]);
        }
        exp.push_back("SphericGuardian");

        int32_t draws = -1;
        const ResolvedGroup g = resolve_at("Sphere and 2 Shapes", seed, draws);
        EXPECT_EQ(members(g), exp) << "seed=" << seed;
        EXPECT_EQ(draws, 2) << "seed=" << seed;
        // Only the SELECTED shape is constructed -- getAncientShape does not
        // build the alternatives, so the construction trace has no discards.
        EXPECT_EQ(g.constructed_count, g.count) << "seed=" << seed;
    }
    EXPECT_TRUE(seen[0] && seen[1] && seen[2])
        << "all three ancient shapes must be reachable";
}

// --- 5. Pool-draw list generation over the new act tables -------------------

// generate_monster_lists is act-parameterised already (S2.12 owns the per-act
// WEAK COUNT -- TheCity/TheBeyond draw two weak entries, not three). What S2.01
// owns is that the tables it reads are well-formed for acts 2 and 3: every drawn
// key resolves, pools are non-degenerate, and the rejection rules can be
// satisfied.
TEST(EncountersS2, ActTwoAndThreeListsDrawOnlyFromTheirOwnPools) {
    for (int32_t act : {2, 3}) {
        for (int64_t seed = 1; seed < 40; ++seed) {
            RngStream a = from_seed(seed);
            RngStream b = from_seed(seed);
            MonsterLists la{};
            MonsterLists lb{};
            generate_monster_lists(act, a, la);
            generate_monster_lists(act, b, lb);
            ASSERT_EQ(a.counter, b.counter) << "act " << act;
            ASSERT_EQ(la.monster_list_count, lb.monster_list_count);
            for (std::size_t i = 0; i < la.monster_list_count; ++i) {
                EXPECT_EQ(la.monster_list[i], lb.monster_list[i]);
            }

            // Membership only: WHERE the weak segment ends is the per-act weak
            // COUNT, which is run-layer state S2.12 owns (TheCity/TheBeyond draw
            // two weak entries, Exordium three). What this pins is that no draw
            // can ever leave the act's own pools.
            for (std::size_t i = 0; i < la.monster_list_count; ++i) {
                const bool in_act_pools =
                    row_of(la.monster_list[i], act, EncounterPool::WEAK) !=
                        nullptr ||
                    row_of(la.monster_list[i], act, EncounterPool::STRONG) !=
                        nullptr;
                EXPECT_TRUE(in_act_pools)
                    << "act " << act << " list position " << i << " key "
                    << la.monster_list[i];
            }
            for (std::size_t i = 0; i < la.elite_list_count; ++i) {
                EXPECT_NE(row_of(la.elite_list[i], act, EncounterPool::ELITE),
                          nullptr);
            }
            ASSERT_EQ(la.boss_list_count, 3u);
            std::vector<sv> bosses(la.boss_list.begin(),
                                   la.boss_list.begin() + la.boss_list_count);
            std::sort(bosses.begin(), bosses.end());
            const std::vector<sv> expected =
                act == 2 ? std::vector<sv>{"Automaton", "Champ", "Collector"}
                         : std::vector<sv>{"Awakened One", "Donu and Deca",
                                           "Time Eater"};
            EXPECT_EQ(bosses, expected) << "act " << act;
        }
    }
}
