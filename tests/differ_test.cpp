// The diff harness: trace format + field-by-field differ + reproducer emitter +
// fixture oracle (design doc §8, §9).
//   * synthetic divergence in EACH field group is caught and named -- one test
//     per group (header, player scalars, player powers, card pool, each pile,
//     monster scalars, monster powers, each queue, each of the 5 RNG streams,
//     the bookkeeping flags), asserting the report is non-empty, names the right
//     field, and (for single-field mutations) reports exactly that one field.
//   * reproducer file replays to the SAME diff -- write/read a reproducer, then
//     re-drive its (seed, actions) through combat_begin+advance and confirm the
//     freshly-replayed state diffs against the same corrupted target with a
//     byte-identical DiffReport.
//   * trace round-trip -- write_trace/read_trace reproduces every state/action.
//   * schema-version rejection -- a mutated stamp is refused, not loaded.
//   * hash fast-path -- identical states diff empty; a change in an expensive
//     region (card_pool) is still caught.

#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "sts/diff/diff_harness.hpp"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/state_hash.hpp"
#include "sts/engine/types.hpp"

namespace sts::diff {
namespace {

using engine::Action;
using engine::ActionQueueItem;
using engine::ActionVerb;
using engine::CardId;
using engine::CardPoolIndex;
using engine::CardQueueItem;
using engine::CombatPhase;
using engine::CombatState;
using engine::hash_state;
using engine::make_action;
using engine::MonsterId;
using engine::MonsterQueueItem;
using engine::PowerId;
using engine::PowerSlot;
using engine::RngStream;
using engine::SCHEMA_VERSION;

// A rich, internally value-initialized CombatState with every field group
// populated so a single-field mutation of a copy yields exactly one (or, for a
// count change, a minimal) named diff. Value-init first (padding-stable), then
// only live fields are written.
CombatState MakeBase() {
    CombatState s{};

    // header
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    s.turn = 3;
    s.flags = 0;

    // player scalars
    s.player_hp = 70;
    s.player_max_hp = 80;
    s.player_block = 5;
    s.player_energy = 3;
    s.stance = 0;
    s.cards_played_this_turn = 2;
    s.player_power_count = 2;
    s.player_powers[0] = PowerSlot{static_cast<uint16_t>(PowerId::STRENGTH), 3};
    s.player_powers[1] = PowerSlot{static_cast<uint16_t>(PowerId::VULNERABLE), 2};

    // card pool (rows 0..5 occupied; the rest are value-init NONE)
    for (int i = 0; i < 6; ++i) {
        s.card_pool[i].card_id = static_cast<uint16_t>(CardId::STRIKE);
        s.card_pool[i].cost_now = 1;
    }
    s.card_pool[5].card_id = static_cast<uint16_t>(CardId::BASH);
    s.card_pool[5].cost_now = 2;

    // piles
    s.hand_count = 4;
    for (int i = 0; i < 4; ++i) s.hand[i] = static_cast<CardPoolIndex>(i);
    s.draw_count = 6;
    for (int i = 0; i < 6; ++i) s.draw[i] = static_cast<CardPoolIndex>(i);
    s.discard_count = 3;
    for (int i = 0; i < 3; ++i) s.discard[i] = static_cast<CardPoolIndex>(i);
    s.exhaust_count = 2;
    for (int i = 0; i < 2; ++i) s.exhaust[i] = static_cast<CardPoolIndex>(i);
    s.limbo_count = 2;
    for (int i = 0; i < 2; ++i) s.limbo[i] = static_cast<CardPoolIndex>(i);

    // monsters
    s.monster_count = 2;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = 44;
    s.monsters[0].max_hp = 44;
    s.monsters[0].block = 0;
    s.monsters[0].move_history[0] = 1;
    s.monsters[0].intent = 1;
    s.monsters[0].power_count = 1;
    s.monsters[0].powers[0] = PowerSlot{static_cast<uint16_t>(PowerId::STRENGTH), 5};
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[1].hp = 40;
    s.monsters[1].max_hp = 44;

    // action queue: 2 live items at head 0
    s.action_head = 0;
    s.action_tail = 2;
    s.action_count = 2;
    s.action_queue[0] = ActionQueueItem{3u, 0xFFu, 0u, 6, 0u};
    s.action_queue[1] = ActionQueueItem{1u, 0xFFu, 0u, 8, 0u};

    // pre-turn queue: 1 live item
    s.pre_turn_head = 0;
    s.pre_turn_tail = 1;
    s.pre_turn_count = 1;
    s.pre_turn_actions[0] = ActionQueueItem{5u, 0xFFu, 0u, 3, 0u};

    // card queue: 1 live item
    s.card_queue_count = 1;
    s.card_queue[0] = CardQueueItem{static_cast<CardPoolIndex>(2), 0u};

    // monster queue: 2 live items
    s.monster_queue_count = 2;
    s.monster_queue[0] = MonsterQueueItem{0u, 0u};
    s.monster_queue[1] = MonsterQueueItem{1u, 0u};

    // bookkeeping
    s.turn_has_ended = 0;
    s.monster_attacks_queued = 1;

    // RNG streams: distinct so each is individually testable
    s.monster_hp_rng = RngStream{1, 2, 3, 0};
    s.ai_rng = RngStream{10, 20, 30, 0};
    s.shuffle_rng = RngStream{100, 200, 300, 0};
    s.card_random_rng = RngStream{1000, 2000, 3000, 0};
    s.misc_rng = RngStream{5, 6, 7, 0};

    return s;
}

// --- unique temp path -------------------------------------------------------

std::string TempPath(const std::string& name) {
    static int counter = 0;
    std::filesystem::path dir = std::filesystem::temp_directory_path();
    return (dir / ("differ_test_" + std::to_string(counter++) + "_" + name)).string();
}

// =============================================================================
// Field-group divergence tests: each mutates exactly one field group.
// =============================================================================

TEST(DifferGroupHeader, Phase) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.phase = static_cast<uint8_t>(CombatPhase::COMBAT_OVER);
    DiffReport r = diff_states(base, mut);
    EXPECT_FALSE(r.empty());
    EXPECT_TRUE(r.mentions("phase")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupHeader, Turn) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.turn = static_cast<uint16_t>(base.turn + 1);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("turn")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupHeader, Flags) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.flags = 0xABCDu;
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("flags")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupPlayer, Hp) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.player_hp = static_cast<int16_t>(base.player_hp - 7);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("player.hp")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupPlayer, Block) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.player_block = static_cast<int16_t>(base.player_block + 4);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("player.block")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupPlayer, Energy) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.player_energy = static_cast<int16_t>(base.player_energy - 1);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("player.energy")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupPlayer, Stance) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.stance = 1;
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("player.stance")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupPlayer, CardsPlayedThisTurn) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.cards_played_this_turn = static_cast<uint8_t>(base.cards_played_this_turn + 1);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("player.cards_played_this_turn")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupPlayer, PowerCount) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.player_power_count = static_cast<uint8_t>(base.player_power_count + 1);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("player.power_count")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupPlayerPowers, PowerIdAndAmount) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.player_powers[0].amount = static_cast<int16_t>(base.player_powers[0].amount + 2);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("player_powers[0].amount")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();

    CombatState mut2 = base;
    mut2.player_powers[1].power_id = static_cast<uint16_t>(PowerId::WEAK);
    DiffReport r2 = diff_states(base, mut2);
    EXPECT_TRUE(r2.mentions("player_powers[1].power_id")) << r2.to_string();
    EXPECT_EQ(r2.size(), 1u) << r2.to_string();
}

TEST(DifferGroupCardPool, Fields) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.card_pool[3].cost_now = static_cast<uint8_t>(base.card_pool[3].cost_now + 1);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("card_pool[3].cost_now")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();

    CombatState mut2 = base;
    mut2.card_pool[0].card_id = static_cast<uint16_t>(CardId::DEFEND);
    DiffReport r2 = diff_states(base, mut2);
    EXPECT_TRUE(r2.mentions("card_pool[0].card_id")) << r2.to_string();
    EXPECT_EQ(r2.size(), 1u) << r2.to_string();
}

TEST(DifferGroupPiles, HandElement) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.hand[2] = static_cast<CardPoolIndex>(9);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("hand[2]")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupPiles, DrawElement) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.draw[4] = static_cast<CardPoolIndex>(9);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("draw[4]")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupPiles, DiscardElement) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.discard[1] = static_cast<CardPoolIndex>(9);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("discard[1]")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupPiles, ExhaustElement) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.exhaust[0] = static_cast<CardPoolIndex>(9);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("exhaust[0]")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupPiles, LimboElement) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.limbo[1] = static_cast<CardPoolIndex>(9);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("limbo[1]")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupPiles, HandCountAndNewElement) {
    // A count change surfaces both the count and the newly-live element.
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.hand_count = static_cast<uint8_t>(base.hand_count + 1);
    mut.hand[base.hand_count] = static_cast<CardPoolIndex>(7);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("hand_count")) << r.to_string();
    EXPECT_TRUE(r.mentions("hand[4]")) << r.to_string();
}

TEST(DifferGroupMonsters, ScalarFields) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.monsters[0].hp = static_cast<int16_t>(base.monsters[0].hp - 10);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("monsters[0].hp")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();

    CombatState mut2 = base;
    mut2.monsters[1].block = 12;
    DiffReport r2 = diff_states(base, mut2);
    EXPECT_TRUE(r2.mentions("monsters[1].block")) << r2.to_string();
    EXPECT_EQ(r2.size(), 1u) << r2.to_string();

    CombatState mut3 = base;
    mut3.monsters[0].move_history[0] =
        static_cast<uint8_t>(base.monsters[0].move_history[0] + 1);
    DiffReport r3 = diff_states(base, mut3);
    EXPECT_TRUE(r3.mentions("monsters[0].move_history[0]")) << r3.to_string();
    EXPECT_EQ(r3.size(), 1u) << r3.to_string();

    CombatState mut4 = base;
    mut4.monster_count = 1;
    DiffReport r4 = diff_states(base, mut4);
    EXPECT_TRUE(r4.mentions("monster_count")) << r4.to_string();
    EXPECT_EQ(r4.size(), 1u) << r4.to_string();
}

TEST(DifferGroupMonsterPowers, PowerSlot) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.monsters[0].powers[0].amount =
        static_cast<int16_t>(base.monsters[0].powers[0].amount + 3);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("monsters[0].powers[0].amount")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupActionQueue, Element) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.action_queue[0].opcode = static_cast<uint16_t>(base.action_queue[0].opcode + 100);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("action_queue[0].opcode")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupPreTurnQueue, Element) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.pre_turn_actions[0].amount = base.pre_turn_actions[0].amount + 5;
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("pre_turn_actions[0].amount")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupCardQueue, Element) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.card_queue[0].card_index = static_cast<CardPoolIndex>(9);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("card_queue[0].card_index")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupMonsterQueue, Element) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.monster_queue[1].monster_index = static_cast<uint8_t>(3);
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("monster_queue[1].monster_index")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(DifferGroupBookkeeping, Flags) {
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.turn_has_ended = 1;
    DiffReport r = diff_states(base, mut);
    EXPECT_TRUE(r.mentions("turn_has_ended")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();

    CombatState mut2 = base;
    mut2.monster_attacks_queued = 0;
    DiffReport r2 = diff_states(base, mut2);
    EXPECT_TRUE(r2.mentions("monster_attacks_queued")) << r2.to_string();
    EXPECT_EQ(r2.size(), 1u) << r2.to_string();
}

// Each RNG stream individually, so an RNG divergence is attributable to the
// specific stream.
TEST(DifferGroupRng, EachStreamNamedSeparately) {
    struct Case {
        const char* name;
        RngStream CombatState::*member;
    };
    const Case cases[] = {
        {"monster_hp_rng", &CombatState::monster_hp_rng},
        {"ai_rng", &CombatState::ai_rng},
        {"shuffle_rng", &CombatState::shuffle_rng},
        {"card_random_rng", &CombatState::card_random_rng},
        {"misc_rng", &CombatState::misc_rng},
    };
    for (const Case& c : cases) {
        // counter divergence
        CombatState base = MakeBase();
        CombatState mut = base;
        (mut.*(c.member)).counter = (base.*(c.member)).counter + 1;
        DiffReport r = diff_states(base, mut);
        EXPECT_TRUE(r.mentions(std::string(c.name) + ".counter"))
            << c.name << ": " << r.to_string();
        EXPECT_EQ(r.size(), 1u) << c.name << ": " << r.to_string();

        // s0 divergence -> proves per-stream attribution, not just "some rng"
        CombatState mut2 = base;
        (mut2.*(c.member)).s0 = (base.*(c.member)).s0 + 1;
        DiffReport r2 = diff_states(base, mut2);
        EXPECT_TRUE(r2.mentions(std::string(c.name) + ".s0"))
            << c.name << ": " << r2.to_string();
        EXPECT_EQ(r2.size(), 1u) << c.name << ": " << r2.to_string();
    }
}

// =============================================================================
// Fast-path + identity
// =============================================================================

TEST(DifferFastPath, IdenticalStatesDiffEmpty) {
    CombatState base = MakeBase();
    CombatState copy = base;  // byte-identical -> hashes equal -> fast path
    ASSERT_EQ(hash_state(base), hash_state(copy));
    DiffReport r = diff_states(base, copy);
    EXPECT_TRUE(r.empty()) << r.to_string();
}

TEST(DifferFastPath, ExpensiveRegionStillWalkedWhenHashDiffers) {
    // A change deep in the 160-row card pool (a region the fast path would skip
    // if hashes matched) is caught precisely because the hashes DON'T match.
    CombatState base = MakeBase();
    CombatState mut = base;
    mut.card_pool[7].card_id = static_cast<uint16_t>(CardId::DEFEND);
    ASSERT_NE(hash_state(base), hash_state(mut));
    DiffReport r = diff_states(base, mut);
    EXPECT_FALSE(r.empty());
    EXPECT_TRUE(r.mentions("card_pool[7].card_id")) << r.to_string();
}

// =============================================================================
// Trace round-trip + schema rejection (driven through a real fight)
// =============================================================================

std::vector<Action> ThreeEndTurns() {
    return {make_action(ActionVerb::END_TURN), make_action(ActionVerb::END_TURN),
            make_action(ActionVerb::END_TURN)};
}

TEST(TraceRoundTrip, WriteReadReproducesEveryStateAndAction) {
    const int64_t seed = 0x5EED1234;
    const std::vector<Action> actions = ThreeEndTurns();
    std::vector<CombatState> states;
    replay_skeleton(seed, actions, states);
    ASSERT_EQ(states.size(), actions.size() + 1);

    const std::string path = TempPath("trace.bin");
    ASSERT_TRUE(write_trace(path, seed, actions, states));

    TraceHeader h{};
    std::vector<TraceRecord> recs;
    ASSERT_TRUE(read_trace(path, h, recs));

    EXPECT_EQ(h.seed, seed);
    EXPECT_EQ(h.schema_version, SCHEMA_VERSION);
    EXPECT_EQ(h.state_size, static_cast<uint32_t>(sizeof(CombatState)));
    ASSERT_EQ(recs.size(), states.size());

    for (std::size_t i = 0; i < states.size(); ++i) {
        EXPECT_EQ(hash_state(recs[i].state), hash_state(states[i])) << "record " << i;
        EXPECT_EQ(std::memcmp(&recs[i].state, &states[i], sizeof(CombatState)), 0)
            << "record " << i;
        const uint32_t expect_action = (i == 0) ? 0u : actions[i - 1].bits;
        EXPECT_EQ(recs[i].action, expect_action) << "record " << i;
    }
}

TEST(TraceSchemaRejection, MutatedSchemaVersionRefused) {
    const int64_t seed = 0x1111;
    const std::vector<Action> actions = ThreeEndTurns();
    std::vector<CombatState> states;
    replay_skeleton(seed, actions, states);

    const std::string path = TempPath("badschema.bin");
    ASSERT_TRUE(write_trace(path, seed, actions, states));

    // Corrupt the stamped schema_version at file offset 4 (right after magic[4]).
    {
        std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(f.is_open());
        f.seekp(4, std::ios::beg);
        const uint32_t bad = SCHEMA_VERSION + 99u;
        f.write(reinterpret_cast<const char*>(&bad), sizeof(bad));
        ASSERT_TRUE(static_cast<bool>(f));
    }

    TraceHeader h{};
    std::vector<TraceRecord> recs;
    EXPECT_FALSE(read_trace(path, h, recs));
}

TEST(TraceSchemaRejection, CorruptMagicRefused) {
    const int64_t seed = 0x2222;
    const std::vector<Action> actions = ThreeEndTurns();
    std::vector<CombatState> states;
    replay_skeleton(seed, actions, states);

    const std::string path = TempPath("badmagic.bin");
    ASSERT_TRUE(write_trace(path, seed, actions, states));

    {
        std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(f.is_open());
        f.seekp(0, std::ios::beg);
        const char x = 'X';
        f.write(&x, 1);
    }

    TraceHeader h{};
    std::vector<TraceRecord> recs;
    EXPECT_FALSE(read_trace(path, h, recs));
}

// =============================================================================
// Reproducer round-trip + "replays to the same diff"
// =============================================================================

TEST(Reproducer, RoundTripReplaysToSameDiff) {
    const int64_t seed = 0x600D5EED;
    const std::vector<Action> actions = ThreeEndTurns();

    // Drive the real fight; take the final state as the engine's "good" output.
    std::vector<CombatState> states;
    replay_skeleton(seed, actions, states);
    const CombatState good = states.back();

    // Simulate "the oracle disagreed here": corrupt one field to make the target.
    CombatState bad = good;
    bad.player_hp = static_cast<int16_t>(good.player_hp - 5);

    const DiffReport orig = diff_states(good, bad);
    ASSERT_FALSE(orig.empty());
    ASSERT_TRUE(orig.mentions("player.hp")) << orig.to_string();

    // Save the reproducer (prefix = full action list; the diverging step is last).
    const std::string path = TempPath("repro.txt");
    ASSERT_TRUE(write_reproducer(path, seed, actions));

    int64_t seed2 = 0;
    std::vector<Action> actions2;
    ASSERT_TRUE(read_reproducer(path, seed2, actions2));
    EXPECT_EQ(seed2, seed);
    ASSERT_EQ(actions2.size(), actions.size());
    for (std::size_t i = 0; i < actions.size(); ++i) {
        EXPECT_EQ(actions2[i].bits, actions[i].bits) << "action " << i;
    }

    // Replay the read-back (seed, actions) and diff against the SAME bad target.
    std::vector<CombatState> states2;
    replay_skeleton(seed2, actions2, states2);
    const CombatState replayed = states2.back();

    // Deterministic replay => replayed == good => identical DiffReport.
    ASSERT_EQ(hash_state(replayed), hash_state(good));
    const DiffReport again = diff_states(replayed, bad);
    ASSERT_EQ(again.size(), orig.size()) << again.to_string();
    for (std::size_t i = 0; i < again.size(); ++i) {
        EXPECT_EQ(again.diffs[i].field_name, orig.diffs[i].field_name);
        EXPECT_EQ(again.diffs[i].expected_repr, orig.diffs[i].expected_repr);
        EXPECT_EQ(again.diffs[i].actual_repr, orig.diffs[i].actual_repr);
    }
}

// =============================================================================
// Fixture oracle
// =============================================================================

TEST(FixtureOracle, QueryReturnsRecordedStateForPrefix) {
    const int64_t seed = 0x0F1C7002;  // any fixed seed
    const std::vector<Action> actions = ThreeEndTurns();
    std::vector<CombatState> states;
    replay_skeleton(seed, actions, states);

    // A fixture file IS a trace file.
    const std::string path = TempPath("fixture.bin");
    ASSERT_TRUE(write_trace(path, seed, actions, states));

    FixtureFileOracleAdapter oracle;
    ASSERT_TRUE(oracle.load_fixture(path));
    EXPECT_EQ(oracle.fixture_count(), 1u);

    CombatState out{};

    // prefix length 0 -> initial state
    ASSERT_TRUE(oracle.query(seed, std::span<const Action>{}, out));
    EXPECT_EQ(hash_state(out), hash_state(states[0]));

    // prefix length 2 -> state after 2 actions
    ASSERT_TRUE(oracle.query(seed, std::span<const Action>(actions).subspan(0, 2), out));
    EXPECT_EQ(hash_state(out), hash_state(states[2]));

    // full prefix -> final state
    ASSERT_TRUE(oracle.query(seed, std::span<const Action>(actions), out));
    EXPECT_EQ(hash_state(out), hash_state(states.back()));

    // unknown seed -> no answer
    EXPECT_FALSE(oracle.query(seed + 1, std::span<const Action>{}, out));

    // prefix longer than the fixture -> no answer
    std::vector<Action> too_long = actions;
    too_long.push_back(make_action(ActionVerb::END_TURN));
    EXPECT_FALSE(oracle.query(seed, std::span<const Action>(too_long), out));

    // prefix that diverges from the recorded actions -> no answer
    std::vector<Action> divergent = actions;
    divergent[1] = make_action(ActionVerb::PLAY_CARD, 0, 0);
    EXPECT_FALSE(oracle.query(seed, std::span<const Action>(divergent), out));
}

}  // namespace
}  // namespace sts::diff
