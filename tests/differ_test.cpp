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
#include "sts/engine/run_state.hpp"
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
    // write_trace is the v1 (CombatState-only) writer; it stamps the v1 format
    // tag, which is decoupled from engine::SCHEMA_VERSION (now 2 after the B1.6
    // v2-container bump). The v2 path is exercised by the TraceV2* tests below.
    EXPECT_EQ(h.schema_version, kTraceFormatV1);
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

// =============================================================================
// B1.6: RunState differ (diff_run_states) -- one test per new field group
// =============================================================================

using engine::CardInstance;
using engine::MapNode;
using engine::RelicSlot;
using engine::RunState;

// A rich, value-initialized RunState with every field group populated so a
// single-field mutation of a copy yields exactly one named diff.
RunState MakeBaseRun() {
    RunState s{};

    // character sheet
    s.run_seed = 0x1234ABCDLL;
    s.hp = 60;
    s.max_hp = 80;
    s.gold = 137;
    s.ascension = 20;
    s.act = 1;
    s.floor = 5;

    // master deck (3 cards, in order)
    s.master_deck_count = 3;
    s.master_deck[0] = CardInstance{static_cast<uint16_t>(CardId::STRIKE), 0, 1, 0, 0};
    s.master_deck[1] = CardInstance{static_cast<uint16_t>(CardId::DEFEND), 0, 1, 0, 0};
    s.master_deck[2] = CardInstance{static_cast<uint16_t>(CardId::BASH), 1, 2, 0, 0};

    // relics (acquisition order == trigger order)
    s.relic_count = 2;
    s.relics[0] = RelicSlot{1, 5};
    s.relics[1] = RelicSlot{2, 0};

    // potions (positional)
    s.potions[0] = 1;
    s.potions[2] = 3;

    // map grid (placeholder encoding; a couple of live nodes)
    s.map[0] = MapNode{1, 2};
    s.map[46] = MapNode{3, 4};

    // boss / keys / event-shop placeholders
    s.boss_ids[0] = 7;
    s.keys = 1;
    s.event_flags = 0xF0u;
    s.shop_flags = 0x0Fu;

    // pity counters
    s.card_blizz_randomizer = 5;
    s.blizzard_potion_mod = 0;

    // the 8 run-level streams (distinct so each is individually testable)
    s.monster_rng = RngStream{1, 2, 3, 0};
    s.event_rng = RngStream{10, 20, 30, 0};
    s.merchant_rng = RngStream{100, 200, 300, 0};
    s.card_rng = RngStream{1000, 2000, 3000, 0};
    s.treasure_rng = RngStream{11, 22, 33, 0};
    s.relic_rng = RngStream{111, 222, 333, 0};
    s.potion_rng = RngStream{5, 6, 7, 0};
    s.map_rng = RngStream{9, 8, 7, 0};

    return s;
}

TEST(RunDifferFastPath, IdenticalRunsDiffEmpty) {
    RunState base = MakeBaseRun();
    RunState copy = base;
    DiffReport r = diff_run_states(base, copy);
    EXPECT_TRUE(r.empty()) << r.to_string();

    RunState z1{};
    RunState z2{};
    EXPECT_TRUE(diff_run_states(z1, z2).empty());
}

TEST(RunDifferCharacterSheet, ScalarFields) {
    RunState base = MakeBaseRun();

    RunState m1 = base;
    m1.run_seed = base.run_seed + 1;
    DiffReport r1 = diff_run_states(base, m1);
    EXPECT_TRUE(r1.mentions("run_seed")) << r1.to_string();
    EXPECT_EQ(r1.size(), 1u) << r1.to_string();

    RunState m2 = base;
    m2.gold = base.gold + 25;
    DiffReport r2 = diff_run_states(base, m2);
    EXPECT_TRUE(r2.mentions("gold")) << r2.to_string();
    EXPECT_EQ(r2.size(), 1u) << r2.to_string();

    RunState m3 = base;
    m3.hp = static_cast<int16_t>(base.hp - 7);
    DiffReport r3 = diff_run_states(base, m3);
    EXPECT_TRUE(r3.mentions("hp")) << r3.to_string();
    EXPECT_EQ(r3.size(), 1u) << r3.to_string();

    RunState m4 = base;
    m4.floor = static_cast<uint16_t>(base.floor + 1);
    DiffReport r4 = diff_run_states(base, m4);
    EXPECT_TRUE(r4.mentions("floor")) << r4.to_string();
    EXPECT_EQ(r4.size(), 1u) << r4.to_string();

    RunState m5 = base;
    m5.ascension = 19;
    DiffReport r5 = diff_run_states(base, m5);
    EXPECT_TRUE(r5.mentions("ascension")) << r5.to_string();
    EXPECT_EQ(r5.size(), 1u) << r5.to_string();
}

TEST(RunDifferDeck, ElementAndCount) {
    RunState base = MakeBaseRun();

    RunState m1 = base;
    m1.master_deck[1].card_id = static_cast<uint16_t>(CardId::SHRUG_IT_OFF);
    DiffReport r1 = diff_run_states(base, m1);
    EXPECT_TRUE(r1.mentions("master_deck[1].card_id")) << r1.to_string();
    EXPECT_EQ(r1.size(), 1u) << r1.to_string();

    RunState m2 = base;
    m2.master_deck[2].upgrade = 0;
    DiffReport r2 = diff_run_states(base, m2);
    EXPECT_TRUE(r2.mentions("master_deck[2].upgrade")) << r2.to_string();
    EXPECT_EQ(r2.size(), 1u) << r2.to_string();

    // count change surfaces the count and the newly-live element
    RunState m3 = base;
    m3.master_deck_count = static_cast<uint16_t>(base.master_deck_count + 1);
    m3.master_deck[base.master_deck_count] =
        CardInstance{static_cast<uint16_t>(CardId::POMMEL_STRIKE), 0, 1, 0, 0};
    DiffReport r3 = diff_run_states(base, m3);
    EXPECT_TRUE(r3.mentions("master_deck_count")) << r3.to_string();
    EXPECT_TRUE(r3.mentions("master_deck[3]")) << r3.to_string();
}

TEST(RunDifferRelics, ElementAndOrder) {
    RunState base = MakeBaseRun();

    RunState m1 = base;
    m1.relics[0].counter = static_cast<int16_t>(base.relics[0].counter + 1);
    DiffReport r1 = diff_run_states(base, m1);
    EXPECT_TRUE(r1.mentions("relics[0].counter")) << r1.to_string();
    EXPECT_EQ(r1.size(), 1u) << r1.to_string();

    RunState m2 = base;
    m2.relics[1].relic_id = 9;
    DiffReport r2 = diff_run_states(base, m2);
    EXPECT_TRUE(r2.mentions("relics[1].relic_id")) << r2.to_string();
    EXPECT_EQ(r2.size(), 1u) << r2.to_string();

    // Order is load-bearing (acquisition == trigger order): swapping two relics
    // is a real divergence, NOT treated as an equal set.
    RunState m3 = base;
    std::swap(m3.relics[0], m3.relics[1]);
    DiffReport r3 = diff_run_states(base, m3);
    EXPECT_FALSE(r3.empty()) << "relic order must be compared positionally";
    EXPECT_TRUE(r3.mentions("relics[0]")) << r3.to_string();
    EXPECT_TRUE(r3.mentions("relics[1]")) << r3.to_string();
}

TEST(RunDifferPotions, Slot) {
    RunState base = MakeBaseRun();
    RunState mut = base;
    mut.potions[2] = 9;
    DiffReport r = diff_run_states(base, mut);
    EXPECT_TRUE(r.mentions("potions[2]")) << r.to_string();
    EXPECT_EQ(r.size(), 1u) << r.to_string();
}

TEST(RunDifferMap, Node) {
    RunState base = MakeBaseRun();

    RunState m1 = base;
    m1.map[46].room_type = static_cast<uint8_t>(base.map[46].room_type + 1);
    DiffReport r1 = diff_run_states(base, m1);
    EXPECT_TRUE(r1.mentions("map[46].room_type")) << r1.to_string();
    EXPECT_EQ(r1.size(), 1u) << r1.to_string();

    RunState m2 = base;
    m2.map[0].edges = static_cast<uint8_t>(base.map[0].edges + 1);
    DiffReport r2 = diff_run_states(base, m2);
    EXPECT_TRUE(r2.mentions("map[0].edges")) << r2.to_string();
    EXPECT_EQ(r2.size(), 1u) << r2.to_string();
}

TEST(RunDifferPlaceholders, BossKeysFlags) {
    RunState base = MakeBaseRun();

    RunState m1 = base;
    m1.boss_ids[0] = 3;
    DiffReport r1 = diff_run_states(base, m1);
    EXPECT_TRUE(r1.mentions("boss_ids[0]")) << r1.to_string();
    EXPECT_EQ(r1.size(), 1u) << r1.to_string();

    RunState m2 = base;
    m2.keys = 7;
    DiffReport r2 = diff_run_states(base, m2);
    EXPECT_TRUE(r2.mentions("keys")) << r2.to_string();
    EXPECT_EQ(r2.size(), 1u) << r2.to_string();

    RunState m3 = base;
    m3.event_flags = 0xDEADu;
    DiffReport r3 = diff_run_states(base, m3);
    EXPECT_TRUE(r3.mentions("event_flags")) << r3.to_string();
    EXPECT_EQ(r3.size(), 1u) << r3.to_string();

    RunState m4 = base;
    m4.shop_flags = 0xBEEFu;
    DiffReport r4 = diff_run_states(base, m4);
    EXPECT_TRUE(r4.mentions("shop_flags")) << r4.to_string();
    EXPECT_EQ(r4.size(), 1u) << r4.to_string();
}

TEST(RunDifferPity, Counters) {
    RunState base = MakeBaseRun();

    RunState m1 = base;
    m1.card_blizz_randomizer = static_cast<int16_t>(base.card_blizz_randomizer - 1);
    DiffReport r1 = diff_run_states(base, m1);
    EXPECT_TRUE(r1.mentions("card_blizz_randomizer")) << r1.to_string();
    EXPECT_EQ(r1.size(), 1u) << r1.to_string();

    RunState m2 = base;
    m2.blizzard_potion_mod = 10;
    DiffReport r2 = diff_run_states(base, m2);
    EXPECT_TRUE(r2.mentions("blizzard_potion_mod")) << r2.to_string();
    EXPECT_EQ(r2.size(), 1u) << r2.to_string();
}

// Each of the 8 run-level streams individually, so a divergence is attributable
// to the specific stream (reusing the combat differ's cmp_stream idiom).
TEST(RunDifferRng, EachStreamNamedSeparately) {
    struct Case {
        const char* name;
        RngStream RunState::*member;
    };
    const Case cases[] = {
        {"monster_rng", &RunState::monster_rng},
        {"event_rng", &RunState::event_rng},
        {"merchant_rng", &RunState::merchant_rng},
        {"card_rng", &RunState::card_rng},
        {"treasure_rng", &RunState::treasure_rng},
        {"relic_rng", &RunState::relic_rng},
        {"potion_rng", &RunState::potion_rng},
        {"map_rng", &RunState::map_rng},
    };
    for (const Case& c : cases) {
        RunState base = MakeBaseRun();
        RunState mut = base;
        (mut.*(c.member)).counter = (base.*(c.member)).counter + 1;
        DiffReport r = diff_run_states(base, mut);
        EXPECT_TRUE(r.mentions(std::string(c.name) + ".counter"))
            << c.name << ": " << r.to_string();
        EXPECT_EQ(r.size(), 1u) << c.name << ": " << r.to_string();

        RunState mut2 = base;
        (mut2.*(c.member)).s0 = (base.*(c.member)).s0 + 1;
        DiffReport r2 = diff_run_states(base, mut2);
        EXPECT_TRUE(r2.mentions(std::string(c.name) + ".s0"))
            << c.name << ": " << r2.to_string();
        EXPECT_EQ(r2.size(), 1u) << c.name << ": " << r2.to_string();
    }
}

// =============================================================================
// B1.6: trace format v2 (state_kind mixed container) + v1 compatibility read
// =============================================================================

// Build a small mixed v2 container: a RUN header record, a COMBAT record, and a
// second RUN record, with meaningful action bits.
std::vector<TraceRecordV2> MakeMixedRecords(const std::vector<Action>& actions) {
    std::vector<TraceRecordV2> recs;

    TraceRecordV2 r0;
    r0.kind = StateKind::RUN;
    r0.run = MakeBaseRun();
    r0.action = 0;  // initial record
    r0.aux = 0;
    recs.push_back(r0);

    TraceRecordV2 r1;
    r1.kind = StateKind::COMBAT;
    r1.combat = MakeBase();
    r1.action = actions[0].bits;
    r1.aux = 0;
    recs.push_back(r1);

    TraceRecordV2 r2;
    r2.kind = StateKind::RUN;
    r2.run = MakeBaseRun();
    r2.run.floor = 6;  // advanced a floor
    r2.action = actions[1].bits;
    r2.aux = 0;
    recs.push_back(r2);

    return recs;
}

TEST(TraceV2RoundTrip, MixedContainerReadsBackByteIdentical) {
    const int64_t seed = 0x5EEDF00D;
    const std::vector<Action> actions = ThreeEndTurns();
    const std::vector<TraceRecordV2> recs = MakeMixedRecords(actions);

    const std::string path = TempPath("trace_v2.bin");
    ASSERT_TRUE(write_trace_v2(path, seed, recs));

    TraceHeaderV2 h{};
    std::vector<TraceRecordV2> back;
    ASSERT_TRUE(read_trace_v2(path, h, back));

    EXPECT_EQ(h.seed, seed);
    EXPECT_EQ(h.schema_version, kTraceFormatV2);
    EXPECT_EQ(h.combat_state_size, static_cast<uint32_t>(sizeof(CombatState)));
    EXPECT_EQ(h.run_state_size, static_cast<uint32_t>(sizeof(engine::RunState)));
    ASSERT_EQ(back.size(), recs.size());

    for (std::size_t i = 0; i < recs.size(); ++i) {
        EXPECT_EQ(static_cast<int>(back[i].kind), static_cast<int>(recs[i].kind)) << i;
        EXPECT_EQ(back[i].action, recs[i].action) << i;
        EXPECT_EQ(back[i].aux, recs[i].aux) << i;
        if (recs[i].kind == StateKind::COMBAT) {
            EXPECT_EQ(std::memcmp(&back[i].combat, &recs[i].combat, sizeof(CombatState)), 0)
                << "combat record " << i;
        } else {
            EXPECT_EQ(std::memcmp(&back[i].run, &recs[i].run, sizeof(engine::RunState)), 0)
                << "run record " << i;
        }
    }
}

TEST(TraceV2Refusal, WrongCombatSizeRefused) {
    const std::vector<Action> actions = ThreeEndTurns();
    const std::string path = TempPath("v2_badcombat.bin");
    ASSERT_TRUE(write_trace_v2(path, 0x11, MakeMixedRecords(actions)));

    // combat_state_size lives at offset 8 (magic[4] + version u32).
    {
        std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(f.is_open());
        f.seekp(8, std::ios::beg);
        const uint32_t bad = static_cast<uint32_t>(sizeof(CombatState)) + 8u;
        f.write(reinterpret_cast<const char*>(&bad), sizeof(bad));
    }

    TraceHeaderV2 h{};
    std::vector<TraceRecordV2> back;
    EXPECT_FALSE(read_trace_v2(path, h, back));
}

TEST(TraceV2Refusal, WrongRunSizeRefused) {
    const std::vector<Action> actions = ThreeEndTurns();
    const std::string path = TempPath("v2_badrun.bin");
    ASSERT_TRUE(write_trace_v2(path, 0x22, MakeMixedRecords(actions)));

    // run_state_size lives at offset 12 (magic[4] + version u32 + combat_size u32).
    {
        std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(f.is_open());
        f.seekp(12, std::ios::beg);
        const uint32_t bad = static_cast<uint32_t>(sizeof(engine::RunState)) + 8u;
        f.write(reinterpret_cast<const char*>(&bad), sizeof(bad));
    }

    TraceHeaderV2 h{};
    std::vector<TraceRecordV2> back;
    EXPECT_FALSE(read_trace_v2(path, h, back));
}

TEST(TraceV2Refusal, UnknownRecordKindRefused) {
    const std::vector<Action> actions = ThreeEndTurns();
    const std::string path = TempPath("v2_badkind.bin");
    ASSERT_TRUE(write_trace_v2(path, 0x33, MakeMixedRecords(actions)));

    // The first record's state_kind byte is at offset 32 (past the 32-byte header).
    {
        std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(f.is_open());
        f.seekp(32, std::ios::beg);
        const uint8_t bad = 0xFF;
        f.write(reinterpret_cast<const char*>(&bad), sizeof(bad));
    }

    TraceHeaderV2 h{};
    std::vector<TraceRecordV2> back;
    EXPECT_FALSE(read_trace_v2(path, h, back));
}

// The v1 fixtures must keep loading after the bump: read_trace_v2 compat-reads a
// v1 (CombatState-only) trace, exposing every record as a COMBAT record.
TEST(TraceV2Compat, ReadsV1TraceAsCombatRecords) {
    const int64_t seed = 0x0C0FFEE;
    const std::vector<Action> actions = ThreeEndTurns();
    std::vector<CombatState> states;
    replay_skeleton(seed, actions, states);

    const std::string path = TempPath("v1_for_compat.bin");
    ASSERT_TRUE(write_trace(path, seed, actions, states));  // a genuine v1 file

    // v1 reader still works (unchanged Stage-A path).
    TraceHeader h1{};
    std::vector<TraceRecord> v1recs;
    ASSERT_TRUE(read_trace(path, h1, v1recs));
    EXPECT_EQ(h1.schema_version, kTraceFormatV1);

    // v2 reader compat-reads the same file.
    TraceHeaderV2 h2{};
    std::vector<TraceRecordV2> v2recs;
    ASSERT_TRUE(read_trace_v2(path, h2, v2recs));
    EXPECT_EQ(h2.schema_version, kTraceFormatV1);
    EXPECT_EQ(h2.run_state_size, 0u);  // v1 had no run records
    EXPECT_EQ(h2.seed, seed);
    ASSERT_EQ(v2recs.size(), states.size());
    for (std::size_t i = 0; i < states.size(); ++i) {
        EXPECT_EQ(static_cast<int>(v2recs[i].kind), static_cast<int>(StateKind::COMBAT)) << i;
        EXPECT_EQ(std::memcmp(&v2recs[i].combat, &states[i], sizeof(CombatState)), 0) << i;
    }
}

// =============================================================================
// B1.6: CommunicationModOracleAdapter over a v2 (run+combat) container
// =============================================================================

TEST(CommunicationModOracle, QueryBothKindsAndNegativeCases) {
    const int64_t seed = 0xABCDEF01;
    const std::vector<Action> actions = ThreeEndTurns();
    const std::vector<TraceRecordV2> recs = MakeMixedRecords(actions);
    // recs: [0]=RUN (initial), [1]=COMBAT (after actions[0]), [2]=RUN (after actions[1]).

    const std::string path = TempPath("adapter_run.bin");
    ASSERT_TRUE(write_trace_v2(path, seed, recs));

    CommunicationModOracleAdapter oracle;
    ASSERT_TRUE(oracle.load_run_trace(path));
    EXPECT_EQ(oracle.run_count(), 1u);

    // prefix length 0 -> record[0], a RUN record.
    RunState run_out{};
    ASSERT_TRUE(oracle.query_run(seed, std::span<const Action>{}, run_out));
    EXPECT_EQ(std::memcmp(&run_out, &recs[0].run, sizeof(RunState)), 0);
    // ... and querying it as combat fails (wrong kind).
    CombatState combat_out{};
    EXPECT_FALSE(oracle.query(seed, std::span<const Action>{}, combat_out));

    // prefix length 1 (actions[0]) -> record[1], a COMBAT record.
    ASSERT_TRUE(oracle.query(seed, std::span<const Action>(actions).subspan(0, 1), combat_out));
    EXPECT_EQ(hash_state(combat_out), hash_state(recs[1].combat));
    // ... and querying it as run fails (wrong kind).
    EXPECT_FALSE(oracle.query_run(seed, std::span<const Action>(actions).subspan(0, 1), run_out));

    // prefix length 2 (actions[0..1]) -> record[2], a RUN record.
    ASSERT_TRUE(oracle.query_run(seed, std::span<const Action>(actions).subspan(0, 2), run_out));
    EXPECT_EQ(run_out.floor, 6);  // the advanced-floor RUN record

    // unknown seed -> no answer (both kinds).
    EXPECT_FALSE(oracle.query(seed + 1, std::span<const Action>{}, combat_out));
    EXPECT_FALSE(oracle.query_run(seed + 1, std::span<const Action>{}, run_out));

    // prefix longer than the run -> no answer.
    std::vector<Action> too_long = actions;  // 3 actions, but only 2 recorded
    EXPECT_FALSE(oracle.query_run(seed, std::span<const Action>(too_long), run_out));

    // prefix that diverges from the recorded action bits -> no answer.
    std::vector<Action> divergent = {make_action(ActionVerb::PLAY_CARD, 0, 0)};
    EXPECT_FALSE(oracle.query(seed, std::span<const Action>(divergent), combat_out));
}

}  // namespace
}  // namespace sts::diff
