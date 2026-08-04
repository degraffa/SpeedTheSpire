// PublicView (T0.1): combat block + v1 schema skeleton.
//
//   * Layout walk -- every byte of PublicView (and its element types) belongs
//     to a declared member, the same discipline state_test enforces on
//     RunState. PublicView is memcmp'd by the T0.5 twin tests and hashed by
//     T0.7's public_hash, so implicit padding would be the exact Windows-only
//     nondeterminism bug the RunState incident documents (run_state.hpp).
//   * Round-trip spot checks over a POPULATED combat state: player scalars
//     the ObsBuffer stub already carried PLUS everything it omitted -- player
//     powers, monster block/flags/move_history, full 24-slot per-monster power
//     lists, discard/exhaust/limbo contents, the potion belt.
//   * The information boundary: two states differing ONLY in hidden data
//     (draw-pile order, RNG stream state, monster construction scratch,
//     resolution-queue contents) encode byte-identically -- the property the
//     T0.5 hidden-twin suite scales up.
//   * Runic Dome suppression parity with encode_observation (the T0.1
//     acceptance line): both encoders hide the telegraphed intent, and only
//     it, and neither touches CombatState.
//   * Zero allocation, via the same TU-local counting operator new/delete
//     pattern as observation_test (the std::sort of the draw multiset must
//     stay in-place).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>

#include <gtest/gtest.h>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/observation.hpp"
#include "sts/engine/public_view.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/types.hpp"

// --- Global allocation counter (this TU only) --------------------------------
// Same shape and rationale as observation_test.cpp: every replaceable global
// form is overridden and routed through one malloc/free pair (asan's
// alloc-dealloc-mismatch fires otherwise), and assertions only ever compare a
// snapshot taken tightly around the call under test.

namespace {
std::atomic<std::size_t> g_alloc_count{0};
}

static void* counted_alloc(std::size_t n) {
    ++g_alloc_count;
    if (n == 0) n = 1;
    return std::malloc(n);
}
void* operator new(std::size_t n) {
    void* p = counted_alloc(n);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n) {
    void* p = counted_alloc(n);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    return counted_alloc(n);
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    return counted_alloc(n);
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

namespace sts::engine {
namespace {

// --- Regression-guard static asserts (mirror the header source of truth) -----

static_assert(std::is_trivially_copyable_v<PublicView>);
static_assert(sizeof(PvCard) == 6);
static_assert(sizeof(PvPower) == 6);
static_assert(sizeof(PvMonster) == 20 + 6 * kPowerCap);
static_assert(sizeof(PublicView) == 3760);

// --- Layout walk (the header's promised "no implicit padding" proof) ---------

struct MemberSpan {
    std::size_t begin;
    std::size_t end;
    const char* name;
};

#define STS_MEMBER_SPAN(T, m) \
    MemberSpan{offsetof(T, m), offsetof(T, m) + sizeof(T::m), #m}

template <typename T, std::size_t N>
std::string find_holes(const MemberSpan (&members)[N], std::size_t total,
                       const char* type_name) {
    std::string holes;
    std::size_t cursor = 0;
    for (const MemberSpan& m : members) {
        if (m.begin != cursor) {
            holes += "\n  [" + std::to_string(cursor) + ", " +
                     std::to_string(m.begin) + ") before " + type_name +
                     "::" + m.name + "  (" + std::to_string(m.begin - cursor) +
                     " bytes)";
        }
        cursor = m.end;
    }
    if (cursor != total) {
        holes += "\n  [" + std::to_string(cursor) + ", " +
                 std::to_string(total) + ") tail  (" +
                 std::to_string(total - cursor) + " bytes)";
    }
    return holes;
}

TEST(PublicViewLayout, PvCardHasNoImplicitPadding) {
    const MemberSpan members[] = {
        STS_MEMBER_SPAN(PvCard, card_id),
        STS_MEMBER_SPAN(PvCard, upgrade),
        STS_MEMBER_SPAN(PvCard, cost_now),
        STS_MEMBER_SPAN(PvCard, flags),
    };
    EXPECT_EQ(find_holes<PvCard>(members, sizeof(PvCard), "PvCard"), "");
}

TEST(PublicViewLayout, PvPowerHasNoImplicitPadding) {
    const MemberSpan members[] = {
        STS_MEMBER_SPAN(PvPower, power_id),
        STS_MEMBER_SPAN(PvPower, amount),
        STS_MEMBER_SPAN(PvPower, counter),
    };
    EXPECT_EQ(find_holes<PvPower>(members, sizeof(PvPower), "PvPower"), "");
}

TEST(PublicViewLayout, PvMonsterHasNoImplicitPadding) {
    const MemberSpan members[] = {
        STS_MEMBER_SPAN(PvMonster, monster_id),
        STS_MEMBER_SPAN(PvMonster, hp),
        STS_MEMBER_SPAN(PvMonster, max_hp),
        STS_MEMBER_SPAN(PvMonster, block),
        STS_MEMBER_SPAN(PvMonster, flags),
        STS_MEMBER_SPAN(PvMonster, move_history),
        STS_MEMBER_SPAN(PvMonster, intent),
        STS_MEMBER_SPAN(PvMonster, occupied),
        STS_MEMBER_SPAN(PvMonster, power_count),
        STS_MEMBER_SPAN(PvMonster, pad0),
        STS_MEMBER_SPAN(PvMonster, powers),
    };
    EXPECT_EQ(find_holes<PvMonster>(members, sizeof(PvMonster), "PvMonster"),
              "");
}

TEST(PublicViewLayout, PublicViewHasNoImplicitPadding) {
    const MemberSpan members[] = {
        STS_MEMBER_SPAN(PublicView, public_view_version),
        STS_MEMBER_SPAN(PublicView, run_phase),
        STS_MEMBER_SPAN(PublicView, combat_active),
        STS_MEMBER_SPAN(PublicView, combat_phase),
        STS_MEMBER_SPAN(PublicView, stance),
        STS_MEMBER_SPAN(PublicView, turn),
        STS_MEMBER_SPAN(PublicView, combat_gold),
        STS_MEMBER_SPAN(PublicView, combat_flags),
        STS_MEMBER_SPAN(PublicView, player_hp),
        STS_MEMBER_SPAN(PublicView, player_max_hp),
        STS_MEMBER_SPAN(PublicView, player_block),
        STS_MEMBER_SPAN(PublicView, player_energy),
        STS_MEMBER_SPAN(PublicView, cards_played_this_turn),
        STS_MEMBER_SPAN(PublicView, player_power_count),
        STS_MEMBER_SPAN(PublicView, hand_count),
        STS_MEMBER_SPAN(PublicView, draw_count),
        STS_MEMBER_SPAN(PublicView, discard_count),
        STS_MEMBER_SPAN(PublicView, exhaust_count),
        STS_MEMBER_SPAN(PublicView, limbo_count),
        STS_MEMBER_SPAN(PublicView, monster_count),
        STS_MEMBER_SPAN(PublicView, player_powers),
        STS_MEMBER_SPAN(PublicView, hand),
        STS_MEMBER_SPAN(PublicView, draw),
        STS_MEMBER_SPAN(PublicView, discard),
        STS_MEMBER_SPAN(PublicView, exhaust),
        STS_MEMBER_SPAN(PublicView, limbo),
        STS_MEMBER_SPAN(PublicView, monsters),
        STS_MEMBER_SPAN(PublicView, potions),
        STS_MEMBER_SPAN(PublicView, potion_slot_count),
        STS_MEMBER_SPAN(PublicView, keys_reserved),
        STS_MEMBER_SPAN(PublicView, boss_relic_choice_reserved),
        STS_MEMBER_SPAN(PublicView, second_boss_reserved),
        STS_MEMBER_SPAN(PublicView, act_reserved),
        STS_MEMBER_SPAN(PublicView, pad_tail),
    };
    const std::string holes =
        find_holes<PublicView>(members, sizeof(PublicView), "PublicView");
    EXPECT_TRUE(holes.empty())
        << "PublicView has bytes that belong to no member:" << holes
        << "\nPublicView is memcmp'd by the twin tests and hashed by "
           "public_hash, so every byte must be a written member. Declare an "
           "explicit pad member covering each gap.";
}

// --- Sample-state builders ----------------------------------------------------

// A combat state exercising every field the encoder carries: player powers
// (with a live `counter`), non-trivial piles in all five zones, a monster with
// block / flags / move_history and a FULL 24-slot power list, and a second
// monster so slot handling is visible.
CombatState make_combat() {
    CombatState s{};
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    s.turn = 3;
    s.flags = kCombatFlagEliteRoom | kCombatFlagArtOfWarAttackPlayed;
    s.combat_gold = 25;

    s.player_hp = 61;
    s.player_max_hp = 75;
    s.player_block = 9;
    s.player_energy = 2;
    s.stance = 0;
    s.cards_played_this_turn = 2;

    s.player_power_count = 2;
    s.player_powers[0] =
        PowerSlot{static_cast<uint16_t>(PowerId::STRENGTH), 2, 0, 0};
    // THE_BOMB: amount = fuse turns, counter = the constructed damage -- the
    // second oracle-visible number ObsPower could not carry.
    s.player_powers[1] =
        PowerSlot{static_cast<uint16_t>(PowerId::THE_BOMB), 2, 40, 0};

    // Card pool. Pool indices are deliberately non-contiguous and unordered so
    // the tests prove the view carries VALUES, never indices.
    s.card_pool[4] = CardInstance{static_cast<uint16_t>(CardId::BASH), 1, 2,
                                  card_flag_bit(CardFlag::FREE_TO_PLAY_ONCE), 0};
    s.card_pool[1] =
        CardInstance{static_cast<uint16_t>(CardId::DEFEND), 0, 1, 0, 0};
    s.card_pool[7] =
        CardInstance{static_cast<uint16_t>(CardId::STRIKE), 0, 1, 0, 0};
    s.card_pool[2] =
        CardInstance{static_cast<uint16_t>(CardId::STRIKE), 1, 1, 0, 0};
    s.card_pool[9] =
        CardInstance{static_cast<uint16_t>(CardId::DEFEND), 0, 1, 0, 0};

    s.hand[0] = 4;  // Bash+ (free to play once)
    s.hand[1] = 1;  // Defend
    s.hand_count = 2;
    // Draw pile in a deliberately non-canonical engine order: Strike+ (id 1,
    // upgraded), Defend, Strike. Canonical sort is ascending (card_id,
    // upgrade, ...): STRIKE(1,u0), STRIKE(1,u1), DEFEND(2,u0).
    s.draw[0] = 2;  // Strike+
    s.draw[1] = 9;  // Defend
    s.draw[2] = 7;  // Strike
    s.draw_count = 3;
    s.discard[0] = 7;  // (aliasing pool rows across piles is fine for encoding)
    s.discard_count = 1;
    s.exhaust[0] = 1;
    s.exhaust_count = 1;
    s.limbo[0] = 2;
    s.limbo_count = 1;

    // Monster 0: full power list, block, flags, history.
    s.monster_count = 2;
    MonsterState& m0 = s.monsters[0];
    m0.monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    m0.hp = 30;
    m0.max_hp = 44;
    m0.block = 6;
    m0.flags = kMonsterFlagRitualSkip;
    m0.move_history[0] = 3;
    m0.move_history[1] = 1;
    m0.move_history[2] = 2;
    m0.intent = 7;
    m0.power_count = static_cast<uint8_t>(kPowerCap);
    for (int p = 0; p < kPowerCap; ++p) {
        m0.powers[p] = PowerSlot{static_cast<uint16_t>(PowerId::WEAK),
                                 static_cast<int16_t>(p + 1),
                                 static_cast<int16_t>(p % 2), 0};
    }

    MonsterState& m1 = s.monsters[1];
    m1.monster_id = static_cast<uint16_t>(MonsterId::CULTIST);
    m1.hp = 50;
    m1.max_hp = 54;
    m1.block = 0;
    m1.intent = 2;
    m1.power_count = 1;
    m1.powers[0] = PowerSlot{static_cast<uint16_t>(PowerId::RITUAL), 5, 0, 0};
    return s;
}

RunController make_run_in_combat() {
    RunController rc{};
    rc.phase = static_cast<uint8_t>(RunPhase::COMBAT);
    rc.combat = make_combat();
    rc.run.potions[0] = 3;
    rc.run.potions[1] = 11;
    rc.run.potion_slots = 2;
    return rc;
}

// --- Header / version ---------------------------------------------------------

TEST(PublicView, VersionStampAndHeaderEcho) {
    const RunController rc = make_run_in_combat();
    PublicView pv{};
    encode_public_view(rc, pv);

    EXPECT_EQ(pv.public_view_version, PUBLIC_VIEW_VERSION);
    EXPECT_EQ(pv.run_phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(pv.combat_active, 1);
    EXPECT_EQ(pv.combat_phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
}

// --- Combat round trip ---------------------------------------------------------

TEST(PublicView, PlayerScalarsAndPowersRoundTrip) {
    const RunController rc = make_run_in_combat();
    PublicView pv{};
    encode_public_view(rc, pv);

    EXPECT_EQ(pv.turn, 3);
    EXPECT_EQ(pv.combat_gold, 25);
    EXPECT_EQ(pv.combat_flags,
              kCombatFlagEliteRoom | kCombatFlagArtOfWarAttackPlayed);
    EXPECT_EQ(pv.player_hp, 61);
    EXPECT_EQ(pv.player_max_hp, 75);
    EXPECT_EQ(pv.player_block, 9);
    EXPECT_EQ(pv.player_energy, 2);
    EXPECT_EQ(pv.stance, 0);
    EXPECT_EQ(pv.cards_played_this_turn, 2);

    // Player powers -- absent from the ObsBuffer stub entirely, including the
    // second per-instance number (`counter`).
    EXPECT_EQ(pv.player_power_count, 2);
    EXPECT_EQ(pv.player_powers[0].power_id,
              static_cast<uint16_t>(PowerId::STRENGTH));
    EXPECT_EQ(pv.player_powers[0].amount, 2);
    EXPECT_EQ(pv.player_powers[1].power_id,
              static_cast<uint16_t>(PowerId::THE_BOMB));
    EXPECT_EQ(pv.player_powers[1].amount, 2);
    EXPECT_EQ(pv.player_powers[1].counter, 40);
    for (int p = 2; p < kPowerCap; ++p) {
        EXPECT_EQ(pv.player_powers[p].power_id, 0u) << "slot " << p;
    }
}

TEST(PublicView, PilesRoundTripAndDrawIsCanonicallySorted) {
    const RunController rc = make_run_in_combat();
    PublicView pv{};
    encode_public_view(rc, pv);

    // Hand: engine order, full per-instance state (flags word included --
    // FREE_TO_PLAY_ONCE is information cost_now does not carry).
    EXPECT_EQ(pv.hand_count, 2);
    EXPECT_EQ(pv.hand[0].card_id, static_cast<uint16_t>(CardId::BASH));
    EXPECT_EQ(pv.hand[0].upgrade, 1);
    EXPECT_EQ(pv.hand[0].cost_now, 2);
    EXPECT_EQ(pv.hand[0].flags, card_flag_bit(CardFlag::FREE_TO_PLAY_ONCE));
    EXPECT_EQ(pv.hand[1].card_id, static_cast<uint16_t>(CardId::DEFEND));

    // Draw: the UNORDERED MULTISET in canonical ascending order, not the
    // engine's (hidden) order Strike+, Defend, Strike.
    EXPECT_EQ(pv.draw_count, 3);
    EXPECT_EQ(pv.draw[0].card_id, static_cast<uint16_t>(CardId::STRIKE));
    EXPECT_EQ(pv.draw[0].upgrade, 0);
    EXPECT_EQ(pv.draw[1].card_id, static_cast<uint16_t>(CardId::STRIKE));
    EXPECT_EQ(pv.draw[1].upgrade, 1);
    EXPECT_EQ(pv.draw[2].card_id, static_cast<uint16_t>(CardId::DEFEND));

    // Discard / exhaust / limbo: contents the stub omitted, engine order.
    EXPECT_EQ(pv.discard_count, 1);
    EXPECT_EQ(pv.discard[0].card_id, static_cast<uint16_t>(CardId::STRIKE));
    EXPECT_EQ(pv.exhaust_count, 1);
    EXPECT_EQ(pv.exhaust[0].card_id, static_cast<uint16_t>(CardId::DEFEND));
    EXPECT_EQ(pv.limbo_count, 1);
    EXPECT_EQ(pv.limbo[0].card_id, static_cast<uint16_t>(CardId::STRIKE));
    EXPECT_EQ(pv.limbo[0].upgrade, 1);

    // Unused slots read zero.
    for (int i = 3; i < kDrawCap; ++i) {
        EXPECT_EQ(pv.draw[i].card_id, 0u) << "draw slot " << i;
    }
}

TEST(PublicView, MonsterBlockFlagsHistoryAndFullPowerList) {
    const RunController rc = make_run_in_combat();
    PublicView pv{};
    encode_public_view(rc, pv);

    EXPECT_EQ(pv.monster_count, 2);
    const PvMonster& m0 = pv.monsters[0];
    EXPECT_EQ(m0.monster_id, static_cast<uint16_t>(MonsterId::JAW_WORM));
    EXPECT_EQ(m0.occupied, 1);
    EXPECT_EQ(m0.hp, 30);
    EXPECT_EQ(m0.max_hp, 44);
    EXPECT_EQ(m0.block, 6);              // the stub's biggest gap
    EXPECT_EQ(m0.flags, kMonsterFlagRitualSkip);
    EXPECT_EQ(m0.move_history[0], 3);
    EXPECT_EQ(m0.move_history[1], 1);
    EXPECT_EQ(m0.move_history[2], 2);
    EXPECT_EQ(m0.intent, 7);

    // The FULL 24-slot power list -- no 4-slot truncation, counters included.
    EXPECT_EQ(m0.power_count, kPowerCap);
    for (int p = 0; p < kPowerCap; ++p) {
        EXPECT_EQ(m0.powers[p].power_id, static_cast<uint16_t>(PowerId::WEAK))
            << "power slot " << p;
        EXPECT_EQ(m0.powers[p].amount, p + 1) << "power slot " << p;
        EXPECT_EQ(m0.powers[p].counter, p % 2) << "power slot " << p;
    }
    // (Contrast: the ObsBuffer stub truncates the same monster at 4 slots --
    // kObsMonsterPowerCap in observation.hpp -- which is the gap this closes.)
    static_assert(kObsMonsterPowerCap < kPowerCap);

    const PvMonster& m1 = pv.monsters[1];
    EXPECT_EQ(m1.monster_id, static_cast<uint16_t>(MonsterId::CULTIST));
    EXPECT_EQ(m1.power_count, 1);
    EXPECT_EQ(m1.powers[0].power_id, static_cast<uint16_t>(PowerId::RITUAL));

    // Unoccupied slots are fully zero.
    for (int m = 2; m < kMonsterCap; ++m) {
        EXPECT_EQ(pv.monsters[m].occupied, 0) << "monster slot " << m;
        EXPECT_EQ(pv.monsters[m].monster_id, 0u) << "monster slot " << m;
    }
}

TEST(PublicView, PotionBeltEncodesInEveryPhase) {
    RunController rc = make_run_in_combat();
    PublicView in_combat{};
    encode_public_view(rc, in_combat);
    EXPECT_EQ(in_combat.potions[0], 3u);
    EXPECT_EQ(in_combat.potions[1], 11u);
    EXPECT_EQ(in_combat.potions[2], 0u);
    EXPECT_EQ(in_combat.potion_slot_count, 2);

    rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
    PublicView on_map{};
    encode_public_view(rc, on_map);
    EXPECT_EQ(on_map.potions[0], 3u);
    EXPECT_EQ(on_map.potions[1], 11u);
    EXPECT_EQ(on_map.potion_slot_count, 2);
}

// --- The information boundary ---------------------------------------------------

// Hidden draw ORDER must not reach the bytes: permuting the draw index array
// (and nothing else) encodes byte-identically. This is the seed the T0.5
// hidden-twin suite scales to every hidden source.
TEST(PublicView, DrawOrderPermutationEncodesByteIdentically) {
    const RunController a = make_run_in_combat();
    RunController b = make_run_in_combat();
    b.combat.draw[0] = 9;  // Defend, Strike, Strike+ -- a different hidden order
    b.combat.draw[1] = 7;
    b.combat.draw[2] = 2;

    PublicView pa{};
    PublicView pb{};
    encode_public_view(a, pa);
    encode_public_view(b, pb);
    EXPECT_EQ(std::memcmp(&pa, &pb, sizeof(PublicView)), 0)
        << "hidden draw order leaked into PublicView bytes";
}

// Neither RNG stream state, nor monster construction scratch (pad0 -- the
// Louse's unrevealed bite roll lives there), nor resolution-queue contents may
// reach the bytes.
TEST(PublicView, HiddenAndTransientStateDoesNotLeak) {
    const RunController a = make_run_in_combat();
    RunController b = make_run_in_combat();

    b.combat.shuffle_rng.counter += 17;   // hidden realization state
    b.combat.ai_rng.s0 ^= 0x12345u;
    b.combat.monsters[0].pad0 = 42;       // unrevealed construction scratch
    b.combat.card_pool[1].misc = 7;       // mid-resolution scratch (excluded)
    b.combat.action_queue[0] =            // resolution transient (excluded)
        ActionQueueItem{9, 0, 1, 123, 0};
    b.combat.action_count = 1;
    b.combat.action_tail = 1;
    b.combat.turn_has_ended = 1;

    PublicView pa{};
    PublicView pb{};
    encode_public_view(a, pa);
    encode_public_view(b, pb);
    EXPECT_EQ(std::memcmp(&pa, &pb, sizeof(PublicView)), 0)
        << "hidden/transient CombatState bytes leaked into PublicView";
}

// --- Runic Dome parity with encode_observation ----------------------------------

TEST(PublicView, RunicDomeSuppressionParityWithEncodeObservation) {
    // Without the Dome: both encoders carry the telegraphed intents.
    const RunController plain = make_run_in_combat();
    ObsBuffer obs_plain{};
    PublicView pv_plain{};
    encode_observation(plain.combat, obs_plain);
    encode_public_view(plain, pv_plain);
    for (int m = 0; m < 2; ++m) {
        EXPECT_EQ(pv_plain.monsters[m].intent, obs_plain.monsters[m].intent)
            << "monster " << m;
    }
    ASSERT_EQ(pv_plain.monsters[0].intent, 7) << "probe is wrong";

    // With the Dome: both encoders hide every occupied slot's intent (0), and
    // only the intent -- move_history stays visible (past moves were observed
    // as they resolved).
    RunController domed = make_run_in_combat();
    domed.combat.relics[0] =
        RelicSlot{static_cast<uint16_t>(RelicId::RUNIC_DOME), -1};
    domed.combat.relic_count = 1;
    ObsBuffer obs_domed{};
    PublicView pv_domed{};
    encode_observation(domed.combat, obs_domed);
    encode_public_view(domed, pv_domed);
    for (int m = 0; m < 2; ++m) {
        EXPECT_EQ(pv_domed.monsters[m].intent, 0) << "monster " << m;
        EXPECT_EQ(pv_domed.monsters[m].intent, obs_domed.monsters[m].intent)
            << "monster " << m;
    }
    EXPECT_EQ(pv_domed.monsters[0].move_history[0], 3);
    EXPECT_EQ(pv_domed.monsters[0].move_history[1], 1);
    EXPECT_EQ(pv_domed.monsters[0].move_history[2], 2);
    // Everything else is untouched by the suppression.
    EXPECT_EQ(pv_domed.monsters[0].hp, pv_plain.monsters[0].hp);
    EXPECT_EQ(pv_domed.monsters[0].block, pv_plain.monsters[0].block);
    EXPECT_EQ(pv_domed.monsters[0].power_count, pv_plain.monsters[0].power_count);

    // And the suppression never reaches the simulated state (the capture-diff
    // pipeline anchors on MonsterState.intent -- observation.hpp write-up).
    EXPECT_EQ(domed.combat.monsters[0].intent, 7)
        << "Runic Dome reached MonsterState -- every capture diff is now wrong";
}

// --- Non-combat phases -----------------------------------------------------------

TEST(PublicView, NonCombatPhaseZeroesTheCombatSection) {
    RunController rc = make_run_in_combat();
    rc.phase = static_cast<uint8_t>(RunPhase::COMBAT_REWARD);
    // rc.combat still holds the (finished) combat -- it must NOT be encoded.
    PublicView pv{};
    encode_public_view(rc, pv);

    EXPECT_EQ(pv.run_phase, static_cast<uint8_t>(RunPhase::COMBAT_REWARD));
    EXPECT_EQ(pv.combat_active, 0);
    EXPECT_EQ(pv.combat_phase, 0);
    EXPECT_EQ(pv.turn, 0);
    EXPECT_EQ(pv.player_hp, 0);
    EXPECT_EQ(pv.hand_count, 0);
    EXPECT_EQ(pv.monster_count, 0);
    EXPECT_EQ(pv.monsters[0].monster_id, 0u);
    EXPECT_EQ(pv.monsters[0].occupied, 0);
    // The belt is still public.
    EXPECT_EQ(pv.potions[0], 3u);
}

// --- Full overwrite / reserved fields ---------------------------------------------

// `out` is fully overwritten: encoding into a garbage-filled buffer yields the
// same bytes as encoding into a fresh one (every byte, reserved fields and pad
// members included, is assigned).
TEST(PublicView, EncodeFullyOverwritesWithoutPreZero) {
    const RunController rc = make_run_in_combat();
    PublicView dirty;
    std::memset(&dirty, 0xAB, sizeof(PublicView));
    encode_public_view(rc, dirty);
    PublicView fresh{};
    encode_public_view(rc, fresh);
    EXPECT_EQ(std::memcmp(&dirty, &fresh, sizeof(PublicView)), 0)
        << "encode_public_view left stale bytes in the output buffer";
}

TEST(PublicView, ReservedV1FieldsReadZero) {
    const RunController rc = make_run_in_combat();
    PublicView pv;
    std::memset(&pv, 0xAB, sizeof(PublicView));
    encode_public_view(rc, pv);

    EXPECT_EQ(pv.keys_reserved, 0);
    EXPECT_EQ(pv.boss_relic_choice_reserved[0], 0u);
    EXPECT_EQ(pv.boss_relic_choice_reserved[1], 0u);
    EXPECT_EQ(pv.boss_relic_choice_reserved[2], 0u);
    EXPECT_EQ(pv.second_boss_reserved, 0u);
    EXPECT_EQ(pv.act_reserved, 0);
    EXPECT_EQ(pv.pad_tail[0], 0);
    EXPECT_EQ(pv.pad_tail[1], 0);
    EXPECT_EQ(pv.pad_tail[2], 0);
}

// --- Zero allocation ---------------------------------------------------------------

TEST(PublicView, EncodeDoesNotAllocate) {
    const RunController rc = make_run_in_combat();
    PublicView pv{};
    // Warm anything lazy touched by the arguments BEFORE the measured window.
    encode_public_view(rc, pv);

    const std::size_t before = g_alloc_count;
    encode_public_view(rc, pv);  // <- the only statement in the measured scope
    const std::size_t after = g_alloc_count;

    EXPECT_EQ(after, before) << "encode_public_view allocated on the heap";
}

}  // namespace
}  // namespace sts::engine
