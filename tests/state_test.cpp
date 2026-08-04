// CombatState / RunState layout guarantees + the state-hash / snapshot contract
// (design doc §4.1-§4.3).
//
//   * is_trivially_copyable<CombatState> / <RunState>          -- §4.1
//   * sizeof(CombatState) <= 8192, sizeof(RunState) <= 8192    -- §4.2/§4.3
//   * snapshot = memcpy round-trip is hash-equal (and byte-equal)
//   * two value-initialized states hash-equal (padding determinism, §4.1)
//
// The static_asserts live in the headers (source of truth); the ones here are
// regression guards.

#include <cstddef>
#include <cstring>
#include <string>
#include <type_traits>

#include <gtest/gtest.h>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/state_hash.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

// --- Regression-guard static asserts (mirror the header source of truth) ----

static_assert(std::is_trivially_copyable_v<CombatState>);
static_assert(std::is_trivially_copyable_v<RunState>);
static_assert(sizeof(CombatState) <= 8192);
static_assert(sizeof(RunState) <= 8192);

TEST(StateLayout, TriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<CombatState>);
    EXPECT_TRUE(std::is_trivially_copyable_v<RunState>);
}

TEST(StateLayout, SizeWithinBudget) {
    EXPECT_LE(sizeof(CombatState), 8192u);
    EXPECT_LE(sizeof(RunState), 8192u);
}

// --- Snapshot = memcpy round-trip is hash- and byte-equal -------------------

TEST(StateHash, CombatMemcpyRoundTripIsEqual) {
    CombatState a{};
    // Mutate a spread of fields across groups to non-default values so the
    // round-trip is exercising real content, not an all-zero object.
    a.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    a.turn = 3;
    a.flags = 0xDEADBEEFu;
    a.player_hp = 68;
    a.player_max_hp = 80;
    a.player_block = 12;
    a.player_energy = 3;
    a.cards_played_this_turn = 2;
    a.player_power_count = 1;
    a.player_powers[0] = PowerSlot{static_cast<uint16_t>(PowerId::STRENGTH), 2, 0, 0};
    a.card_pool[0] = CardInstance{static_cast<uint16_t>(CardId::BASH), 0, 2, 0, 0};
    a.hand[0] = 0;
    a.hand_count = 1;
    a.monster_count = 1;
    a.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    a.monsters[0].hp = 44;
    a.monsters[0].max_hp = 44;
    a.monsters[0].move_history[0] = 1;
    a.action_queue[0] = ActionQueueItem{7, 0, 1, 12, 0};
    a.action_count = 1;
    a.card_queue[0] = CardQueueItem{0, 1};
    a.card_queue_count = 1;
    a.monster_hp_rng = from_seed(12345);
    a.ai_rng = from_seed(67890);

    const uint64_t h_a = hash_state(a);

    // Snapshot via raw memcpy (design doc §4.1/§8: snapshot == memcpy), then
    // reconstitute into a fresh object the same way.
    unsigned char buffer[sizeof(CombatState)];
    std::memcpy(buffer, &a, sizeof(CombatState));
    CombatState b{};
    std::memcpy(&b, buffer, sizeof(CombatState));

    EXPECT_EQ(hash_state(b), h_a);
    EXPECT_EQ(std::memcmp(&a, &b, sizeof(CombatState)), 0);
}

TEST(StateHash, RunMemcpyRoundTripIsEqual) {
    RunState a{};
    a.run_seed = -1;
    a.master_deck_count = 12;
    a.master_deck[0] = CardInstance{static_cast<uint16_t>(CardId::STRIKE), 0, 1, 0, 0};
    a.hp = 72;
    a.max_hp = 80;
    a.gold = 99;
    a.ascension = 20;
    a.act = 1;
    a.floor = 6;
    a.relics[0] = RelicSlot{1, 0};
    a.relic_count = 1;
    a.potions[0] = 5;
    a.map[0] = MapNode{2, 0b101};
    a.boss_ids[0] = 3;
    a.keys = 0b010;
    a.card_blizz_randomizer = 5;
    a.blizzard_potion_mod = -10;
    // schema-v3 additive fields (B4.3): exercised so the round-trip covers them.
    a.event_pity_monster = 0.1f;
    a.event_pity_shop = 0.03f;
    a.event_pity_treasure = 0.02f;
    a.purge_cost = 75;
    a.potion_slots = 2;
    a.event_membership = 0x07FFu;
    a.special_membership = 0x3FFFu;
    a.shrine_membership = 0x3Fu;
    a.relic_pool_count[0] = 2;
    a.relic_pools[0][0] = 11;
    a.relic_pools[0][1] = 12;
    a.monster_rng = from_seed(42);
    a.map_rng = from_seed(43);
    a.neow_rng = from_seed(44);

    const uint64_t h_a = hash_state(a);

    unsigned char buffer[sizeof(RunState)];
    std::memcpy(buffer, &a, sizeof(RunState));
    RunState b{};
    std::memcpy(&b, buffer, sizeof(RunState));

    EXPECT_EQ(hash_state(b), h_a);
    EXPECT_EQ(std::memcmp(&a, &b, sizeof(RunState)), 0);
}

// --- Two value-initialized states hash-equal (padding determinism, §4.1) ----
//
// This is the actual check that value-initialization zero-fills padding on this
// toolchain -- if it fails, the struct has non-deterministic padding and the
// byte-hash premise is broken (investigate the struct, do not paper over it).

TEST(StateHash, TwoValueInitializedCombatStatesHashEqual) {
    CombatState a{};
    CombatState b{};
    EXPECT_EQ(hash_state(a), hash_state(b));
    EXPECT_EQ(std::memcmp(&a, &b, sizeof(CombatState)), 0);
}

TEST(StateHash, TwoValueInitializedRunStatesHashEqual) {
    RunState a{};
    RunState b{};
    EXPECT_EQ(hash_state(a), hash_state(b));
    EXPECT_EQ(std::memcmp(&a, &b, sizeof(RunState)), 0);
}

// --- No IMPLICIT padding anywhere in RunState (the byte-compare premise) ----
//
// WHY THE TWO TESTS ABOVE ARE NOT ENOUGH, and what they missed.
//
// They compare two objects whose storage the runtime happened to hand over
// clean -- a fresh stack frame, a fresh heap page. They therefore pass whether
// or not the struct has indeterminate padding, which is the definition of a
// vacuous guard. Under it, a real defect survived: `RunState` carried SIX
// undeclared bytes between `pad_relic_pools` and `monster_rng`, and neither
// `RunState{}` nor a copy of one is required to write them. Aggregate
// initialization ([dcl.init.list]/3 reaches the aggregate bullet before the
// empty-list value-init bullet) initializes each MEMBER; an implicitly-defined
// copy constructor performs a MEMBERWISE copy ([class.copy.ctor]/14). Padding is
// not a member in either sentence. The header comment at that block asserted
// the opposite -- "the compiler inserts (value-init-zeroed) padding ahead of
// this block" -- and nothing tested it.
//
// It surfaced as Translator.RoundTripDeterministic failing on `win-debug` only:
// translating one file twice produced two records whose bytes differed at
// exactly offset 1962, inside that gap. It reads like "the translator is
// nondeterministic"; it was a struct-layout claim nobody had checked. On Linux
// the same code passed because freshly mapped pages arrive zeroed, and a
// runtime probe does not reproduce it either -- clang-cl bulk-writes a
// stack-local `RunState{}`, so only the exact allocation shape the translator
// happens to use exposes the gap.
//
// So the guard is not a probe. It is the layout itself: walk the declared
// members in order and require each to start where the previous one ended. That
// is deterministic on every host and every optimisation level, it fails the
// moment a future field leaves a hole, and its failure message names the hole.
//
// SCOPE, stated rather than implied: this audits RunState's OWN member list.
// Padding *inside* a nested type (CardInstance, RelicSlot, MapNode, RngStream)
// would not be caught here -- those are separately fixed-layout PODs with their
// own asserts, and none of them is where this bug lived.

namespace {

struct MemberSpan {
    std::size_t begin;
    std::size_t end;
    const char* name;
};

#define STS_MEMBER_SPAN(T, m) \
    MemberSpan{offsetof(T, m), offsetof(T, m) + sizeof(T::m), #m}

}  // namespace

// The same property for CombatState, RunController and the transient structs is
// held by the T0.5 classification tripwire (include/sts/engine/byte_class.hpp +
// tests/tripwire_test.cpp), which walks every classified struct's members and
// requires the declared rows to tile its `sizeof`. This RunState-only walk is
// kept rather than folded into it: it is the original conventions §8
// elimination and is cited by name there.
TEST(StateLayout, RunStateHasNoImplicitPadding) {
    const MemberSpan members[] = {
        STS_MEMBER_SPAN(RunState, run_seed),
        STS_MEMBER_SPAN(RunState, master_deck),
        STS_MEMBER_SPAN(RunState, master_deck_count),
        STS_MEMBER_SPAN(RunState, hp),
        STS_MEMBER_SPAN(RunState, max_hp),
        STS_MEMBER_SPAN(RunState, pad_gold_align),
        STS_MEMBER_SPAN(RunState, gold),
        STS_MEMBER_SPAN(RunState, ascension),
        STS_MEMBER_SPAN(RunState, act),
        STS_MEMBER_SPAN(RunState, floor),
        STS_MEMBER_SPAN(RunState, relics),
        STS_MEMBER_SPAN(RunState, relic_count),
        STS_MEMBER_SPAN(RunState, pad_relic),
        STS_MEMBER_SPAN(RunState, potions),
        STS_MEMBER_SPAN(RunState, map),
        STS_MEMBER_SPAN(RunState, boss_ids),
        STS_MEMBER_SPAN(RunState, keys),
        STS_MEMBER_SPAN(RunState, pad_keys),
        STS_MEMBER_SPAN(RunState, event_flags),
        STS_MEMBER_SPAN(RunState, shop_flags),
        STS_MEMBER_SPAN(RunState, card_blizz_randomizer),
        STS_MEMBER_SPAN(RunState, blizzard_potion_mod),
        STS_MEMBER_SPAN(RunState, event_pity_monster),
        STS_MEMBER_SPAN(RunState, event_pity_shop),
        STS_MEMBER_SPAN(RunState, event_pity_treasure),
        STS_MEMBER_SPAN(RunState, purge_cost),
        STS_MEMBER_SPAN(RunState, potion_slots),
        STS_MEMBER_SPAN(RunState, pad_potion_slots),
        STS_MEMBER_SPAN(RunState, event_membership),
        STS_MEMBER_SPAN(RunState, special_membership),
        STS_MEMBER_SPAN(RunState, shrine_membership),
        STS_MEMBER_SPAN(RunState, pad_membership),
        STS_MEMBER_SPAN(RunState, relic_pools),
        STS_MEMBER_SPAN(RunState, relic_pool_count),
        STS_MEMBER_SPAN(RunState, pad_relic_pools),
        STS_MEMBER_SPAN(RunState, pad_rng_align),
        STS_MEMBER_SPAN(RunState, monster_rng),
        STS_MEMBER_SPAN(RunState, event_rng),
        STS_MEMBER_SPAN(RunState, merchant_rng),
        STS_MEMBER_SPAN(RunState, card_rng),
        STS_MEMBER_SPAN(RunState, treasure_rng),
        STS_MEMBER_SPAN(RunState, relic_rng),
        STS_MEMBER_SPAN(RunState, potion_rng),
        STS_MEMBER_SPAN(RunState, map_rng),
        STS_MEMBER_SPAN(RunState, neow_rng),
    };

    // Report EVERY hole in one run. Stopping at the first one costs a rebuild
    // per gap, and the fix is one edit per gap.
    std::string holes;
    std::size_t cursor = 0;
    for (const MemberSpan& m : members) {
        if (m.begin != cursor) {
            holes += "\n  [" + std::to_string(cursor) + ", " +
                     std::to_string(m.begin) + ") before RunState::" + m.name +
                     "  (" + std::to_string(m.begin - cursor) + " bytes)";
        }
        cursor = m.end;
    }
    if (cursor != sizeof(RunState)) {
        holes += "\n  [" + std::to_string(cursor) + ", " +
                 std::to_string(sizeof(RunState)) + ") tail  (" +
                 std::to_string(sizeof(RunState) - cursor) + " bytes)";
    }
    EXPECT_TRUE(holes.empty())
        << "RunState has bytes that belong to no member:" << holes
        << "\nNeither `RunState{}` nor a memberwise copy is required to write "
           "them, and RunState is compared with memcmp and hashed by bytes. "
           "Declare an explicit pad member covering each gap -- that changes no "
           "offsets, it only makes the bytes initialised.";
}

// A mutated state must not collide with a fresh one -- guards against a
// degenerate hash_state that ignores its input.
TEST(StateHash, MutationChangesHash) {
    CombatState a{};
    const uint64_t h0 = hash_state(a);
    a.player_hp = 1;
    EXPECT_NE(hash_state(a), h0);
}

// --- Trap 8: relic trigger order is acquisition order (design doc §10) ------
//
// The skeleton has no relic behavior to actually trigger yet (RelicId is
// sentinel-only), so this pins the claim at the level that's testable now:
// RunState.relics is a plain insertion-ordered array with no sorting/reorder
// path anywhere, so acquisition order IS list order IS (future) trigger order
// by construction. Regresses if that ever stops being true.
TEST(RunStateTrap, RelicsPreserveAcquisitionOrder) {
    RunState s{};
    // Acquire three relics in a specific order; nothing should reorder them.
    s.relics[s.relic_count++] = RelicSlot{301, 0};
    s.relics[s.relic_count++] = RelicSlot{104, 5};
    s.relics[s.relic_count++] = RelicSlot{207, 1};

    ASSERT_EQ(s.relic_count, 3);
    EXPECT_EQ(s.relics[0].relic_id, 301);
    EXPECT_EQ(s.relics[1].relic_id, 104);
    EXPECT_EQ(s.relics[2].relic_id, 207);
}

}  // namespace
}  // namespace sts::engine
