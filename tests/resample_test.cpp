// T0.4 -- resample_hidden, the belief sampler (docs/training-plan.md §2.4).
//
// The suite is organised ONE GROUP PER TABLE ROW, because the acceptance bar is
// per row: each row asserts its own preserve / condition / fresh behaviour on a
// constructed state. Beyond the rows:
//
//   * SamplerEncounterSuffix.ContinuationFromEmptyPrefixReproducesGeneration --
//     the Markov argument made executable: continuing from a zero-length prefix
//     with an identically positioned stream reproduces generate_monster_lists
//     entry for entry. If the continuation ever stops being the same chain,
//     this fails before any distributional test could notice.
//   * SamplerPublicQuantities -- encode_public_view is byte-identical across a
//     resample, plus field-level checks for everything the T0.1 view does not
//     reach yet (map, deck, pity counters, membership bitsets, screens).
//   * SamplerDeterminism -- same sampler seed, same particle, byte for byte.
//   * SamplerPoisonedSeedCanary -- the acceptance line "a particle stepped
//     through a full combat + floor transition never touches the true seed".
//     See the comment on that group for how the canary is built and what a
//     failure would mean.
//
// BYTE COMPARISON DISCIPLINE. A memcmp over RunController is only meaningful
// between objects whose padding provably came from the same bytes -- and while
// the tripwire's EveryByteIsAMember says there is no such padding today (it was
// MonsterLists' std::string_view alignment slack that put this rule here), the
// discipline is cheap and the next struct to grow some should not need to
// rediscover it. Every
// comparison below therefore starts from `clone_bytes`, a std::memcpy of the
// object REPRESENTATION -- not the implicit copy constructor, which is defined
// as memberwise and leaves padding unspecified. That is the same trap
// docs/conventions.md §8 records against RunState.

#include "sts/engine/resample.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "sts/engine/advance.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/knowledge.hpp"
#include "sts/engine/map_rooms.hpp"
#include "sts/engine/monster_dispatch.hpp"  // kMonsterAscension
#include "sts/engine/public_view.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/treasure_rooms.hpp"
#include "sts/registry/event_table.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kA20 = 20;
constexpr int64_t kSamplerSeed = 0x5A3D'11CE;

// --- byte-exact clone (see the header note) ----------------------------------

void clone_bytes(RunController& dst, const RunController& src) {
    std::memcpy(static_cast<void*>(&dst), static_cast<const void*>(&src),
                sizeof(RunController));
}

bool same_bytes(const RunController& a, const RunController& b) {
    return std::memcmp(&a, &b, sizeof(RunController)) == 0;
}

// Build a particle whose PADDING provably came from `truth`'s bytes, so a
// memcmp against `truth` is meaningful (see the header note).
void make_particle(RunController& out, const RunController& truth,
                   int64_t sampler_seed) {
    clone_bytes(out, truth);
    SamplerRng rng = sampler_rng_from_seed(sampler_seed);
    resample_hidden(out, rng);
}

bool streams_equal(const RngStream& a, const RngStream& b) {
    return a.s0 == b.s0 && a.s1 == b.s1 && a.counter == b.counter;
}

// --- run helpers (same shape as run_advance_test) -----------------------------

int64_t find_first_encounter_seed(std::string_view key) {
    for (int64_t s = 1; s < 4000; ++s) {
        RngStream m = from_seed(s);
        MonsterLists ml{};
        generate_monster_lists(1, m, ml);
        if (ml.monster_list_count > 0 &&
            ml.monster_list[0] == encounter_key_id(key)) {
            return s;
        }
    }
    ADD_FAILURE() << "no seed found whose first encounter is " << key;
    return 1;
}

void step(RunController& rc, Action a) {
    StepResult res{};
    advance(std::span<RunController>(&rc, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&res, 1));
}

uint8_t first_legal_column(const RunController& rc) {
    RunActionMask m{};
    legal_actions(rc, m);
    for (uint8_t x = 0; x < kMapCols; ++x) {
        if (m.can_choose_node[x]) return x;
    }
    ADD_FAILURE() << "no legal map column";
    return 0;
}

// Leave Neow without taking a blessing (the run_advance_test rationale: a
// payout moves streams, the deck and the relic pools underneath the test).
void leave_neow(RunController& rc) {
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::DONE);
    step(rc, make_action(ActionVerb::CHOOSE));
}

RunController at_map_choice(int64_t seed) {
    RunController rc = run_begin(seed, kA20);
    leave_neow(rc);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    return rc;
}

RunController in_combat(int64_t seed) {
    RunController rc = at_map_choice(seed);
    step(rc, make_action(ActionVerb::CHOOSE, first_legal_column(rc)));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    return rc;
}

// Play the first legal enemy-target card, else end the turn, until the combat
// leaves the COMBAT phase. Bounded so a stuck loop fails rather than hangs.
void play_out_combat(RunController& rc) {
    for (int i = 0; i < 800; ++i) {
        if (rc.phase != static_cast<uint8_t>(RunPhase::COMBAT)) return;
        RunActionMask m{};
        legal_actions(rc, m);
        Action a = make_action(ActionVerb::END_TURN);
        bool played = false;
        for (int c = 0; c < kHandCap && !played; ++c) {
            for (int t = 0; t < kMonsterCap; ++t) {
                if (m.combat.can_play_target[c][t]) {
                    a = make_action(ActionVerb::PLAY_CARD,
                                    static_cast<uint8_t>(c),
                                    static_cast<uint8_t>(t));
                    played = true;
                    break;
                }
            }
        }
        step(rc, a);
    }
    ADD_FAILURE() << "combat did not terminate within the step cap";
}

// --- synthetic combat states for the draw-order row ---------------------------

// A pile of `n` distinct pool indices, top-first values n-1 .. 0 (index 0 is
// the BOTTOM; piles.hpp puts the top at draw_count-1).
CombatState pile_of(int n) {
    CombatState s{};
    s.draw_count = static_cast<uint8_t>(n);
    for (int i = 0; i < n; ++i) {
        s.draw[i] = static_cast<CardPoolIndex>(i);
    }
    return s;
}

std::vector<int> pile_top_first(const CombatState& s) {
    std::vector<int> out;
    for (int p = 0; p < s.draw_count; ++p) {
        out.push_back(static_cast<int>(s.draw[s.draw_count - 1 - p]));
    }
    return out;
}

std::multiset<int> pile_multiset(const CombatState& s) {
    const std::vector<int> v = pile_top_first(s);
    return std::multiset<int>(v.begin(), v.end());
}

// =============================================================================
// Row: draw-pile order -- "uniformly permute unknown slots subject to
// KnowledgeState order constraints"
// =============================================================================

TEST(SamplerDrawOrder, NoKnowledgePermutesTheWholePileAndPreservesTheMultiset) {
    const CombatState base = pile_of(8);
    bool saw_a_different_order = false;
    for (int64_t seed = 1; seed <= 25; ++seed) {
        CombatState s = base;
        SamplerRng rng = sampler_rng_from_seed(seed);
        resample_draw_order(s, KnowledgeState{}, rng);
        EXPECT_EQ(s.draw_count, base.draw_count);
        EXPECT_EQ(pile_multiset(s), pile_multiset(base));
        if (pile_top_first(s) != pile_top_first(base)) {
            saw_a_different_order = true;
        }
    }
    EXPECT_TRUE(saw_a_different_order)
        << "an unconstrained pile must actually be permuted";
}

TEST(SamplerDrawOrder, ExactPrefixIsPinned) {
    // Headbutt-style: the top two positions are exactly known.
    const CombatState base = pile_of(8);
    KnowledgeState k{};
    k.chain_count = 2;
    k.exact_prefix = 2;
    k.chain[0] = base.draw[7];  // the top card
    k.chain[1] = base.draw[6];
    for (int64_t seed = 1; seed <= 25; ++seed) {
        CombatState s = base;
        SamplerRng rng = sampler_rng_from_seed(seed);
        resample_draw_order(s, k, rng);
        const std::vector<int> got = pile_top_first(s);
        EXPECT_EQ(got[0], static_cast<int>(k.chain[0]));
        EXPECT_EQ(got[1], static_cast<int>(k.chain[1]));
        EXPECT_EQ(pile_multiset(s), pile_multiset(base));
    }
}

TEST(SamplerDrawOrder, RelativeOrderChainSurvivesAndActuallyInterleaves) {
    // The post-random-insertion contract (knowledge.hpp): no absolute position
    // claim survives, but the chain's RELATIVE order does. The sampler must
    // therefore (a) always emit the chain in order and (b) genuinely interleave
    // free cards around it -- including ABOVE the chain head, which is exactly
    // the declared coarsening relative to the JDK mechanic.
    const CombatState base = pile_of(8);
    KnowledgeState k{};
    k.chain_count = 3;
    k.exact_prefix = 0;
    k.chain[0] = base.draw[7];
    k.chain[1] = base.draw[4];
    k.chain[2] = base.draw[1];

    bool saw_free_card_above_chain_head = false;
    for (int64_t seed = 1; seed <= 60; ++seed) {
        CombatState s = base;
        SamplerRng rng = sampler_rng_from_seed(seed);
        resample_draw_order(s, k, rng);
        EXPECT_EQ(pile_multiset(s), pile_multiset(base));

        const std::vector<int> got = pile_top_first(s);
        int next = 0;
        for (std::size_t p = 0; p < got.size(); ++p) {
            if (next < k.chain_count &&
                got[p] == static_cast<int>(k.chain[next])) {
                if (next == 0 && p > 0) saw_free_card_above_chain_head = true;
                ++next;
            }
        }
        EXPECT_EQ(next, k.chain_count)
            << "the chain must appear top-first in its declared relative order";
    }
    EXPECT_TRUE(saw_free_card_above_chain_head)
        << "the uniform-interleave contract must allow an unknown card above "
           "the chain head (this is the declared coarsening of addToRandomSpot)";
}

TEST(SamplerDrawOrder, FullOrderPileIsUntouched) {
    // Frozen Eye: the whole order is public, so there is nothing to sample.
    CombatState base = pile_of(6);
    KnowledgeState k{};
    k.chain_count = 6;
    k.exact_prefix = 6;
    k.full_order = 1;
    for (int i = 0; i < 6; ++i) {
        k.chain[i] = base.draw[5 - i];
    }
    for (int64_t seed = 1; seed <= 10; ++seed) {
        CombatState s = base;
        SamplerRng rng = sampler_rng_from_seed(seed);
        resample_draw_order(s, k, rng);
        EXPECT_EQ(pile_top_first(s), pile_top_first(base));
    }
}

TEST(SamplerDrawOrder, SingleCardAndEmptyPilesAreNoOps) {
    for (int n : {0, 1}) {
        CombatState base = pile_of(n);
        CombatState s = base;
        SamplerRng rng = sampler_rng_from_seed(7);
        resample_draw_order(s, KnowledgeState{}, rng);
        EXPECT_EQ(pile_top_first(s), pile_top_first(base));
    }
}

// =============================================================================
// Row: future intents / in-combat randomness / lazily-drawn sources -- "fresh
// streams", and the pity counters + membership bitsets that ride beside them
// =============================================================================

TEST(SamplerFreshStreams, EveryRefreshedStreamIsFreshAndAtCounterZero) {
    const RunController truth = in_combat(find_first_encounter_seed("Jaw Worm"));
    const RunController p = resample_hidden(truth, kSamplerSeed);

    struct Row {
        const char* name;
        const RngStream& before;
        const RngStream& after;
    };
    const Row rows[] = {
        {"monster_rng", truth.run.monster_rng, p.run.monster_rng},
        {"event_rng", truth.run.event_rng, p.run.event_rng},
        {"merchant_rng", truth.run.merchant_rng, p.run.merchant_rng},
        {"card_rng", truth.run.card_rng, p.run.card_rng},
        {"treasure_rng", truth.run.treasure_rng, p.run.treasure_rng},
        {"relic_rng", truth.run.relic_rng, p.run.relic_rng},
        {"potion_rng", truth.run.potion_rng, p.run.potion_rng},
        {"neow_rng", truth.run.neow_rng, p.run.neow_rng},
        {"map_rng", truth.run.map_rng, p.run.map_rng},
        {"monster_hp_rng", truth.combat.monster_hp_rng, p.combat.monster_hp_rng},
        {"ai_rng", truth.combat.ai_rng, p.combat.ai_rng},
        {"shuffle_rng", truth.combat.shuffle_rng, p.combat.shuffle_rng},
        {"card_random_rng", truth.combat.card_random_rng,
         p.combat.card_random_rng},
        {"misc_rng", truth.combat.misc_rng, p.combat.misc_rng},
    };
    for (const Row& r : rows) {
        // Counter 0 is the zero-engine-draw property made checkable: the
        // sampler seeds a stream and never spends it.
        EXPECT_EQ(r.after.counter, 0) << r.name;
        EXPECT_FALSE(streams_equal(r.before, r.after)) << r.name;
    }

    // Two particles differ in every stream.
    const RunController q = resample_hidden(truth, kSamplerSeed + 1);
    EXPECT_FALSE(streams_equal(p.run.card_rng, q.run.card_rng));
    EXPECT_FALSE(streams_equal(p.combat.ai_rng, q.combat.ai_rng));
    EXPECT_FALSE(streams_equal(p.run.map_rng, q.run.map_rng));
}

TEST(SamplerFreshStreams, PityCountersAndMembershipBitsetsArePreserved) {
    RunController truth = in_combat(find_first_encounter_seed("Jaw Worm"));
    truth.run.card_blizz_randomizer = 3;
    truth.run.blizzard_potion_mod = -10;
    truth.run.event_pity_monster = 0.17f;
    truth.run.event_pity_shop = 0.06f;
    truth.run.event_pity_treasure = 0.04f;
    truth.run.event_membership = 0x0355;
    truth.run.special_membership = 0x1234;
    truth.run.shrine_membership = 0x2A;
    truth.run.event_flags = 0xDEAD;
    truth.run.shop_flags = 0xBEEF;
    truth.run.purge_cost = 125;

    const RunController p = resample_hidden(truth, kSamplerSeed);
    EXPECT_EQ(p.run.card_blizz_randomizer, 3);
    EXPECT_EQ(p.run.blizzard_potion_mod, -10);
    EXPECT_EQ(p.run.event_pity_monster, 0.17f);
    EXPECT_EQ(p.run.event_pity_shop, 0.06f);
    EXPECT_EQ(p.run.event_pity_treasure, 0.04f);
    EXPECT_EQ(p.run.event_membership, 0x0355);
    EXPECT_EQ(p.run.special_membership, 0x1234);
    EXPECT_EQ(p.run.shrine_membership, 0x2A);
    EXPECT_EQ(p.run.event_flags, 0xDEADu);
    EXPECT_EQ(p.run.shop_flags, 0xBEEFu);
    EXPECT_EQ(p.run.purge_cost, 125);
}

// =============================================================================
// Row: floor-stream derivation -- "a fresh fake run seed per particle"
// =============================================================================

TEST(SamplerFakeRunSeed, ParticleGetsItsOwnSeedAndItDrivesTheFakeFuture) {
    const RunController truth = at_map_choice(1234);
    const RunController p = resample_hidden(truth, kSamplerSeed);
    const RunController q = resample_hidden(truth, kSamplerSeed + 1);

    EXPECT_NE(p.run.run_seed, truth.run.run_seed);
    EXPECT_NE(p.run.run_seed, q.run.run_seed);

    // The floor reseed reads the state's OWN run_seed, so a particle stepped
    // across a floor boundary must land on streams the true seed cannot
    // produce. Compare two controllers identical in every byte except the run
    // seed, stepped through the same transition.
    RunController fake;
    clone_bytes(fake, p);
    RunController with_true_seed;
    clone_bytes(with_true_seed, p);
    with_true_seed.run.run_seed = truth.run.run_seed;

    const uint8_t col = first_legal_column(fake);
    next_room_transition(fake, col, /*to_boss=*/false);
    next_room_transition(with_true_seed, col, /*to_boss=*/false);

    EXPECT_EQ(fake.run.floor, with_true_seed.run.floor);
    EXPECT_FALSE(streams_equal(fake.combat.ai_rng, with_true_seed.combat.ai_rng))
        << "the particle's floor streams must derive from its FAKE run seed";
}

// =============================================================================
// Row: encounter-list suffix -- "condition, don't reroll"
// =============================================================================

TEST(SamplerEncounterSuffix, ContinuationFromEmptyPrefixReproducesGeneration) {
    // The Markov argument, executable. Same act, identically positioned stream,
    // zero-length prefix -> the continuation must reproduce the run-start
    // generation entry for entry (the boss shuffle is a separate row and is
    // drawn after both lists, so the streams stay aligned).
    for (int64_t seed : {11LL, 97LL, 4242LL}) {
        RngStream a = from_seed(seed);
        MonsterLists generated{};
        generate_monster_lists(1, a, generated);

        RngStream b = from_seed(seed);
        MonsterLists continued{};
        continued.monster_list_count = generated.monster_list_count;
        continued.elite_list_count = generated.elite_list_count;
        continue_monster_lists(1, b, 0, 0, continued);

        ASSERT_EQ(continued.monster_list_count, generated.monster_list_count);
        for (uint8_t i = 0; i < generated.monster_list_count; ++i) {
            EXPECT_EQ(continued.monster_list[i], generated.monster_list[i])
                << "seed " << seed << " monster index " << int(i);
        }
        ASSERT_EQ(continued.elite_list_count, generated.elite_list_count);
        for (uint8_t i = 0; i < generated.elite_list_count; ++i) {
            EXPECT_EQ(continued.elite_list[i], generated.elite_list[i])
                << "seed " << seed << " elite index " << int(i);
        }
    }
}

TEST(SamplerEncounterSuffix, PrefixPinnedSuffixRedrawnAndChainRulesHold) {
    RunController truth = in_combat(find_first_encounter_seed("Jaw Worm"));
    // Stand two monster rooms in: the cursor counts LEFT rooms, and the room
    // currently occupied is observed too, so three entries are public.
    truth.monster_cursor = 2;
    truth.elite_cursor = 1;
    ASSERT_EQ(truth.room_type, static_cast<uint8_t>(RoomType::Monster));

    std::set<std::string> suffixes;
    for (int64_t seed = 1; seed <= 12; ++seed) {
        const RunController p = resample_hidden(truth, seed);
        ASSERT_EQ(p.lists.monster_list_count, truth.lists.monster_list_count);
        ASSERT_EQ(p.lists.elite_list_count, truth.lists.elite_list_count);

        // Preserved prefix: cursor + the room being stood in.
        for (uint8_t i = 0; i < 3; ++i) {
            EXPECT_EQ(p.lists.monster_list[i], truth.lists.monster_list[i]);
        }
        for (uint8_t i = 0; i < 1; ++i) {
            EXPECT_EQ(p.lists.elite_list[i], truth.lists.elite_list[i]);
        }

        // Chain rules. Index 3 is populateFirstStrongEnemy, which applies
        // NEITHER rule (it only rejects the exclusion set), so it is skipped.
        for (uint8_t i = 1; i < p.lists.monster_list_count; ++i) {
            if (i == 3) continue;
            EXPECT_NE(p.lists.monster_list[i], p.lists.monster_list[i - 1])
                << "immediate repeat at " << int(i);
            if (i >= 2) {
                EXPECT_NE(p.lists.monster_list[i], p.lists.monster_list[i - 2])
                    << "A-B-A at " << int(i);
            }
        }
        for (uint8_t i = 1; i < p.lists.elite_list_count; ++i) {
            EXPECT_NE(p.lists.elite_list[i], p.lists.elite_list[i - 1]);
        }

        std::string key;
        for (uint8_t i = 3; i < p.lists.monster_list_count; ++i) {
            key.append(encounter_key_of(p.lists.monster_list[i]))
                .push_back('|');
        }
        suffixes.insert(key);
    }
    EXPECT_GT(suffixes.size(), 1u) << "the unconsumed suffix must be redrawn";
}

TEST(SamplerEncounterSuffix, TheRoomBeingStoodInIsPartOfTheObservedPrefix) {
    // Same cursor, different room kind: standing in a monster room makes
    // monster_list[cursor] public; standing anywhere else does not.
    RunController truth = in_combat(find_first_encounter_seed("Jaw Worm"));
    truth.monster_cursor = 1;

    const RunController in_room = resample_hidden(truth, 5);
    EXPECT_EQ(in_room.lists.monster_list[1], truth.lists.monster_list[1]);

    RunController elsewhere;
    clone_bytes(elsewhere, truth);
    elsewhere.room_type = static_cast<uint8_t>(RoomType::Rest);
    bool ever_redrawn = false;
    for (int64_t seed = 1; seed <= 40; ++seed) {
        const RunController p = resample_hidden(elsewhere, seed);
        EXPECT_EQ(p.lists.monster_list[0], truth.lists.monster_list[0]);
        if (p.lists.monster_list[1] != truth.lists.monster_list[1]) {
            ever_redrawn = true;
        }
    }
    EXPECT_TRUE(ever_redrawn)
        << "an unentered encounter must not be pinned by the sampler";
}

TEST(SamplerEncounterSuffix, BossListConditionsOnTheirPublicHead) {
    RunController truth = in_combat(find_first_encounter_seed("Jaw Worm"));
    ASSERT_EQ(truth.lists.boss_list_count, 3);

    std::set<std::string> tails;
    for (int64_t seed = 1; seed <= 25; ++seed) {
        const RunController p = resample_hidden(truth, seed);
        ASSERT_EQ(p.lists.boss_list_count, truth.lists.boss_list_count);
        EXPECT_EQ(p.lists.boss_list[0], truth.lists.boss_list[0])
            << "boss_list[0] is public (the act boss is named on the map)";
        std::multiset<EncounterKeyId> before;
        std::multiset<EncounterKeyId> after;
        for (uint8_t i = 0; i < truth.lists.boss_list_count; ++i) {
            before.insert(truth.lists.boss_list[i]);
            after.insert(p.lists.boss_list[i]);
        }
        EXPECT_EQ(before, after);
        std::string key;
        for (uint8_t i = 1; i < p.lists.boss_list_count; ++i) {
            key.append(encounter_key_of(p.lists.boss_list[i])).push_back('|');
        }
        tails.insert(key);
    }
    EXPECT_EQ(tails.size(), 2u)
        << "two remaining bosses -> both orderings must occur";
}

// =============================================================================
// Row: relic-pool remainders
// =============================================================================

TEST(SamplerRelicPools, EachTierRemainderIsRepermutedAndPreserved) {
    const RunController truth = at_map_choice(4321);
    bool any_reordered = false;
    for (int64_t seed = 1; seed <= 10; ++seed) {
        const RunController p = resample_hidden(truth, seed);
        for (int t = 0; t < kRelicTierCount; ++t) {
            ASSERT_EQ(p.run.relic_pool_count[t], truth.run.relic_pool_count[t]);
            std::multiset<uint16_t> before;
            std::multiset<uint16_t> after;
            for (uint8_t i = 0; i < truth.run.relic_pool_count[t]; ++i) {
                before.insert(truth.run.relic_pools[t][i]);
                after.insert(p.run.relic_pools[t][i]);
            }
            EXPECT_EQ(before, after) << "tier " << t;
            for (uint8_t i = 0; i < truth.run.relic_pool_count[t]; ++i) {
                if (p.run.relic_pools[t][i] != truth.run.relic_pools[t][i]) {
                    any_reordered = true;
                }
            }
            // Beyond the live count the storage is untouched zero padding.
            for (int i = p.run.relic_pool_count[t]; i < kRelicPoolCap; ++i) {
                EXPECT_EQ(p.run.relic_pools[t][i],
                          truth.run.relic_pools[t][i]);
            }
        }
    }
    EXPECT_TRUE(any_reordered);
}

TEST(SamplerRelicPools, CanSpawnRejectionCornerCaseIsHandledFromTheStoredPool) {
    // The corner case the plan names: a pop-time canSpawn failure CONSUMES the
    // rejected relic and reroutes to an end-pop, so the remainder is NOT
    // "initial pool minus observed acquisitions" -- two relics left the pool
    // and the player saw one. Girya / Peace Pipe / Shovel are all RARE and all
    // refuse to spawn once two of the three are owned, which makes the
    // rejection constructible without hunting a seed.
    RunController truth = at_map_choice(4321);
    RunState& rs = truth.run;
    constexpr int kRare = static_cast<int>(RelicPool::RARE);
    const uint8_t before_count = rs.relic_pool_count[kRare];
    ASSERT_GE(before_count, 3);
    rs.relic_pools[kRare][0] = static_cast<uint16_t>(RelicId::GIRYA);

    RelicSpawnContext ctx{};
    ctx.campfire_relic_count = 2;  // Girya.canSpawn -> false
    const RelicId got = return_random_relic_key(rs, RelicTier::RARE, ctx);
    ASSERT_NE(got, RelicId::GIRYA);
    ASSERT_EQ(rs.relic_pool_count[kRare], before_count - 2)
        << "the rejected relic must have been consumed as well";

    std::multiset<uint16_t> remainder;
    for (uint8_t i = 0; i < rs.relic_pool_count[kRare]; ++i) {
        remainder.insert(rs.relic_pools[kRare][i]);
    }
    const RunController p = resample_hidden(truth, kSamplerSeed);
    std::multiset<uint16_t> sampled;
    for (uint8_t i = 0; i < p.run.relic_pool_count[kRare]; ++i) {
        sampled.insert(p.run.relic_pools[kRare][i]);
    }
    EXPECT_EQ(sampled, remainder)
        << "the sampler permutes the STORED remainder -- the only place the "
           "post-rejection multiset exists";
}

// =============================================================================
// Row: mid-event hidden state (Match & Keep board)
// =============================================================================

EventDialogState match_board() {
    EventDialogState es{};
    es.event_id =
        static_cast<uint16_t>(sts::registry::EventId::MATCH_AND_KEEP);
    es.screen = 2;      // PLAY
    es.scratch0 = 4;    // attempts left
    es.scratch1 = -1;   // nothing face up
    // Six identities, dealt twice, in a fixed arrangement.
    const uint16_t ids[kEventBoardCap] = {10, 11, 12, 13, 14, 15,
                                          10, 11, 12, 13, 14, 15};
    for (int i = 0; i < kEventBoardCap; ++i) {
        es.board[i].card_id = ids[i];
    }
    return es;
}

TEST(SamplerMatchAndKeep, RevealedFlipsArePinnedAndTheRestIsPermuted) {
    EventDialogState base = match_board();
    base.board[3].taken = 1;   // a matched pair left the board
    base.board[9].taken = 1;
    base.scratch1 = 5;         // one card currently face up

    bool ever_moved = false;
    for (int64_t seed = 1; seed <= 25; ++seed) {
        EventDialogState es = base;
        SamplerRng rng = sampler_rng_from_seed(seed);
        resample_match_and_keep_board(es, rng);

        // Pinned: the taken pair and the face-up card.
        EXPECT_EQ(es.board[3].card_id, base.board[3].card_id);
        EXPECT_EQ(es.board[3].taken, 1);
        EXPECT_EQ(es.board[9].card_id, base.board[9].card_id);
        EXPECT_EQ(es.board[9].taken, 1);
        EXPECT_EQ(es.board[5].card_id, base.board[5].card_id);

        // Multiset preserved across the whole board.
        std::multiset<uint16_t> before;
        std::multiset<uint16_t> after;
        for (int i = 0; i < kEventBoardCap; ++i) {
            before.insert(base.board[i].card_id);
            after.insert(es.board[i].card_id);
        }
        EXPECT_EQ(before, after);

        // Everything else is untouched screen state.
        EXPECT_EQ(es.scratch0, base.scratch0);
        EXPECT_EQ(es.scratch1, base.scratch1);
        EXPECT_EQ(es.screen, base.screen);

        for (int i = 0; i < kEventBoardCap; ++i) {
            if (es.board[i].card_id != base.board[i].card_id) ever_moved = true;
        }
    }
    EXPECT_TRUE(ever_moved) << "face-down cards must actually be permuted";
}

TEST(SamplerMatchAndKeep, OtherEventsAreNotTouched) {
    EventDialogState base = match_board();
    base.event_id = static_cast<uint16_t>(sts::registry::EventId::THE_CLERIC);
    EventDialogState es = base;
    SamplerRng rng = sampler_rng_from_seed(3);
    resample_match_and_keep_board(es, rng);
    EXPECT_EQ(std::memcmp(&es, &base, sizeof(EventDialogState)), 0);
}

// =============================================================================
// Row: current-visit chest -- size public, contents hidden
// =============================================================================

TEST(SamplerTreasureChest, SizeIsPreservedAndContentsAreRedrawnInBand) {
    TreasureChest base{};
    base.size = static_cast<uint8_t>(ChestSize::MEDIUM);
    base.relic_tier = static_cast<uint8_t>(RelicTier::COMMON);
    base.has_gold = 1;

    std::set<int> tiers;
    std::set<int> golds;
    for (int64_t seed = 1; seed <= 60; ++seed) {
        TreasureChest c = base;
        SamplerRng rng = sampler_rng_from_seed(seed);
        resample_treasure_chest_contents(c, rng);
        EXPECT_EQ(c.size, base.size);
        EXPECT_EQ(c.opened, 0);
        // A MEDIUM chest's reachable tiers are exactly Common/Uncommon/Rare.
        EXPECT_TRUE(c.relic_tier == static_cast<uint8_t>(RelicTier::COMMON) ||
                    c.relic_tier == static_cast<uint8_t>(RelicTier::UNCOMMON) ||
                    c.relic_tier == static_cast<uint8_t>(RelicTier::RARE));
        tiers.insert(c.relic_tier);
        golds.insert(c.has_gold);
    }
    EXPECT_GT(tiers.size(), 1u);
    EXPECT_EQ(golds.size(), 2u);
}

TEST(SamplerTreasureChest, AnOpenedChestIsPublicAndUntouched) {
    TreasureChest base{};
    base.size = static_cast<uint8_t>(ChestSize::LARGE);
    base.relic_tier = static_cast<uint8_t>(RelicTier::RARE);
    base.has_gold = 1;
    base.opened = 1;
    for (int64_t seed = 1; seed <= 10; ++seed) {
        TreasureChest c = base;
        SamplerRng rng = sampler_rng_from_seed(seed);
        resample_treasure_chest_contents(c, rng);
        EXPECT_EQ(std::memcmp(&c, &base, sizeof(TreasureChest)), 0);
    }
}

// =============================================================================
// Row: monster HP / construction rolls -- "public once revealed"
// =============================================================================

TEST(SamplerMonsterRolls, RevealedRollPinnedUnrevealedRedrawnHpPreserved) {
    RunController truth = in_combat(find_first_encounter_seed("2 Louse"));
    ASSERT_GE(truth.combat.monster_count, 2);

    // Force the knowledge shape the row is about: slot 0's construction roll
    // was telegraphed (public), slot 1's was not.
    truth.knowledge.monster_roll_known[0] = 1;
    truth.knowledge.monster_roll[0] = truth.combat.monsters[0].pad0;
    truth.knowledge.monster_roll_known[1] = 0;

    const sts::registry::MonsterDef* def = sts::registry::monster_def(
        static_cast<MonsterId>(truth.combat.monsters[1].monster_id));
    ASSERT_NE(def, nullptr);
    const sts::registry::MonsterRollDef* bite = nullptr;
    for (uint8_t i = 0; i < def->roll_count; ++i) {
        if (def->rolls[i].timing ==
                sts::registry::MonsterRollTiming::CONSTRUCTOR_AFTER_HP &&
            def->rolls[i].stream ==
                sts::registry::MonsterRollStream::MONSTER_HP) {
            bite = &def->rolls[i];
            break;
        }
    }
    ASSERT_NE(bite, nullptr) << "a Louse must declare a constructor-time roll";

    std::set<int> seen;
    for (int64_t seed = 1; seed <= 40; ++seed) {
        const RunController p = resample_hidden(truth, seed);
        EXPECT_EQ(p.combat.monsters[0].pad0, truth.combat.monsters[0].pad0)
            << "a telegraphed construction roll is public forever";
        EXPECT_GE(p.combat.monsters[1].pad0, bite->min(kMonsterAscension));
        EXPECT_LE(p.combat.monsters[1].pad0, bite->max(kMonsterAscension));
        for (uint8_t m = 0; m < truth.combat.monster_count; ++m) {
            EXPECT_EQ(p.combat.monsters[m].hp, truth.combat.monsters[m].hp);
            EXPECT_EQ(p.combat.monsters[m].max_hp,
                      truth.combat.monsters[m].max_hp);
        }
        seen.insert(p.combat.monsters[1].pad0);
    }
    EXPECT_GT(seen.size(), 1u) << "an unrevealed roll must actually be redrawn";
}

// =============================================================================
// All public quantities preserved
// =============================================================================

TEST(SamplerPublicQuantities, PublicViewIsByteIdenticalAcrossResample) {
    for (int64_t seed : {11LL, 97LL, 4242LL}) {
        const RunController truth = in_combat(find_first_encounter_seed("Jaw Worm"));
        PublicView before{};
        encode_public_view(truth, before);
        const RunController p = resample_hidden(truth, seed);
        PublicView after{};
        encode_public_view(p, after);
        EXPECT_EQ(std::memcmp(&before, &after, sizeof(PublicView)), 0)
            << "sampler seed " << seed;
    }
}

TEST(SamplerPublicQuantities, RunLevelPublicStateIsPreserved) {
    const RunController truth = in_combat(find_first_encounter_seed("Jaw Worm"));
    RunController p;
    make_particle(p, truth, kSamplerSeed);

    EXPECT_EQ(std::memcmp(p.run.map, truth.run.map, sizeof(truth.run.map)), 0);
    EXPECT_EQ(p.run.master_deck_count, truth.run.master_deck_count);
    EXPECT_EQ(std::memcmp(p.run.master_deck, truth.run.master_deck,
                          sizeof(truth.run.master_deck)),
              0);
    EXPECT_EQ(p.run.relic_count, truth.run.relic_count);
    EXPECT_EQ(std::memcmp(p.run.relics, truth.run.relics,
                          sizeof(truth.run.relics)),
              0);
    EXPECT_EQ(p.run.hp, truth.run.hp);
    EXPECT_EQ(p.run.max_hp, truth.run.max_hp);
    EXPECT_EQ(p.run.gold, truth.run.gold);
    EXPECT_EQ(p.run.floor, truth.run.floor);
    EXPECT_EQ(p.run.act, truth.run.act);
    EXPECT_EQ(p.run.ascension, truth.run.ascension);
    EXPECT_EQ(p.run.keys, truth.run.keys);
    EXPECT_EQ(p.run.potion_slots, truth.run.potion_slots);

    // Transient PUBLIC screen state: reward / shop / rest / Neow / chest /
    // bottle overlay, plus the flow scalars and the cursors themselves.
    EXPECT_EQ(std::memcmp(&p.rewards, &truth.rewards, sizeof(RewardScreen)), 0);
    EXPECT_EQ(std::memcmp(&p.shop, &truth.shop, sizeof(ShopState)), 0);
    EXPECT_EQ(std::memcmp(&p.rest, &truth.rest, sizeof(RestSiteState)), 0);
    EXPECT_EQ(std::memcmp(&p.neow, &truth.neow, sizeof(NeowState)), 0);
    EXPECT_EQ(std::memcmp(&p.treasure_chest, &truth.treasure_chest,
                          sizeof(TreasureChest)),
              0);
    EXPECT_EQ(p.pending_bottle, truth.pending_bottle);
    EXPECT_EQ(p.phase, truth.phase);
    EXPECT_EQ(p.cur_x, truth.cur_x);
    EXPECT_EQ(p.room_type, truth.room_type);
    EXPECT_EQ(p.combat_outcome, truth.combat_outcome);
    EXPECT_EQ(p.monster_cursor, truth.monster_cursor);
    EXPECT_EQ(p.elite_cursor, truth.elite_cursor);
    EXPECT_EQ(p.boss_cursor, truth.boss_cursor);
    EXPECT_EQ(p.emerald_x, truth.emerald_x);
    EXPECT_EQ(p.emerald_y, truth.emerald_y);

    // The knowledge record itself is the player's own memory -- a particle
    // must satisfy it, never rewrite it.
    EXPECT_EQ(std::memcmp(&p.knowledge, &truth.knowledge,
                          sizeof(KnowledgeState)),
              0);
}

// =============================================================================
// Determinism
// =============================================================================

TEST(SamplerDeterminism, SameSamplerSeedYieldsAnIdenticalParticle) {
    const RunController truth = in_combat(find_first_encounter_seed("Jaw Worm"));
    RunController a;
    RunController b;
    clone_bytes(a, truth);
    clone_bytes(b, truth);
    SamplerRng ra = sampler_rng_from_seed(kSamplerSeed);
    SamplerRng rb = sampler_rng_from_seed(kSamplerSeed);
    resample_hidden(a, ra);
    resample_hidden(b, rb);
    EXPECT_TRUE(same_bytes(a, b));
    EXPECT_TRUE(streams_equal(ra.stream, rb.stream))
        << "the sampler must consume the same number of private draws";

    RunController c;
    clone_bytes(c, truth);
    SamplerRng rc_rng = sampler_rng_from_seed(kSamplerSeed + 1);
    resample_hidden(c, rc_rng);
    EXPECT_FALSE(same_bytes(a, c));
}

// =============================================================================
// The poisoned-seed canary
// =============================================================================
//
// The acceptance line is "a particle stepped through a full combat + floor
// transition never touches the true seed". The canary makes that checkable
// without instrumenting the engine:
//
//   * TRUTH is a real controller in combat.
//   * POISONED is TRUTH with every hidden randomness carrier replaced by
//     nonsense -- the run seed and all fourteen streams. Nothing PUBLIC differs
//     (run_seed is classified hidden by plan §2.6b, and stream state is the
//     realization, not the rule), so both are members of the same information
//     state and the belief sampler is obliged to map them to the same particle.
//   * Resampling both with ONE sampler seed must give byte-identical particles.
//     Any path by which the true seed -- or anything derived from it -- reached
//     the particle would show up here as a byte difference, because in POISONED
//     that value is different.
//   * The two particles are then stepped through the rest of the combat, the
//     reward screen and a floor transition (which is where the floor-stream
//     reseed reads run_seed) and compared again at every stage. A leak that
//     only opens at the reseed -- an ambient seed, a stale copy in a transient
//     struct -- is caught there rather than at construction.

void poison(RunController& rc) {
    constexpr int64_t kPoison = 0x0BAD'0BAD'0BAD'0BADLL;
    rc.run.run_seed = kPoison;
    rc.run.monster_rng = from_seed(kPoison + 1);
    rc.run.event_rng = from_seed(kPoison + 2);
    rc.run.merchant_rng = from_seed(kPoison + 3);
    rc.run.card_rng = from_seed(kPoison + 4);
    rc.run.treasure_rng = from_seed(kPoison + 5);
    rc.run.relic_rng = from_seed(kPoison + 6);
    rc.run.potion_rng = from_seed(kPoison + 7);
    rc.run.map_rng = from_seed(kPoison + 8);
    rc.run.neow_rng = from_seed(kPoison + 9);
    rc.combat.monster_hp_rng = from_seed(kPoison + 10);
    rc.combat.ai_rng = from_seed(kPoison + 11);
    rc.combat.shuffle_rng = from_seed(kPoison + 12);
    rc.combat.card_random_rng = from_seed(kPoison + 13);
    rc.combat.misc_rng = from_seed(kPoison + 14);
}

TEST(SamplerPoisonedSeedCanary, ParticleNeverTouchesTheTrueSeedOrItsStreams) {
    const RunController truth = in_combat(find_first_encounter_seed("Jaw Worm"));

    RunController poisoned;
    clone_bytes(poisoned, truth);
    poison(poisoned);
    ASSERT_NE(poisoned.run.run_seed, truth.run.run_seed);

    RunController a;
    RunController b;
    clone_bytes(a, truth);
    clone_bytes(b, poisoned);
    SamplerRng ra = sampler_rng_from_seed(kSamplerSeed);
    SamplerRng rb = sampler_rng_from_seed(kSamplerSeed);
    resample_hidden(a, ra);
    resample_hidden(b, rb);
    ASSERT_TRUE(same_bytes(a, b))
        << "the particle must be a function of (public state, sampler seed) "
           "alone -- a difference here is a true-seed leak";

    // ... and it stays a function of them across a full combat, its reward
    // screen and the floor transition that reseeds the floor streams.
    play_out_combat(a);
    play_out_combat(b);
    ASSERT_TRUE(same_bytes(a, b)) << "diverged during combat";
    ASSERT_EQ(a.phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));

    for (int i = 0; i < 8 && a.phase == static_cast<uint8_t>(RunPhase::COMBAT_REWARD);
         ++i) {
        const Action proceed = make_action(ActionVerb::CHOOSE, kChooseProceed);
        step(a, proceed);
        step(b, proceed);
        ASSERT_TRUE(same_bytes(a, b)) << "diverged on the reward screen";
    }
    ASSERT_EQ(a.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

    const uint16_t floor_before = a.run.floor;
    const Action node = make_action(ActionVerb::CHOOSE, first_legal_column(a));
    step(a, node);
    step(b, node);
    EXPECT_EQ(a.run.floor, floor_before + 1) << "the floor transition ran";
    EXPECT_TRUE(same_bytes(a, b)) << "diverged across the floor reseed";
}

}  // namespace
}  // namespace sts::engine
