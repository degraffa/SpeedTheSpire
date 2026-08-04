// PublicView (T0.1): combat block + v1 schema skeleton.
// PublicView (T0.2, v2): the run always-block, the per-phase screen sections,
// the RunActionMask observation channel and the KnowledgeState projection --
// appended below the T0.1 cases, under their own banner. The T0.2 acceptance
// bar lives there: every screen's on-screen contents round-trip, the treasure
// chest is masked until it is opened (including byte-equality of two states
// that differ ONLY in unopened chest contents), and a Headbutt-known top card
// and a Frozen-Eye full-order pile each round-trip their order constraints.
//
//   * Layout walk -- every byte of PublicView (and its element types) belongs
//     to a declared member, the same discipline state_test enforces on
//     RunState. PublicView is memcmp'd by the T0.5 twin tests and hashed by
//     T0.7's public_hash, so implicit padding would be the exact Windows-only
//     nondeterminism bug the RunState incident documents (run_state.hpp).
//   * Round-trip spot checks over a POPULATED combat state: player scalars
//     the OmniscientObsBuffer stub already carried PLUS everything it omitted -- player
//     powers, monster block/flags/move_history, full 24-slot per-monster power
//     lists, discard/exhaust/limbo contents, the potion belt.
//   * The information boundary: two states differing ONLY in hidden data
//     (draw-pile order, RNG stream state, monster construction scratch,
//     resolution-queue contents) encode byte-identically -- the property the
//     T0.5 hidden-twin suite scales up.
//   * Runic Dome suppression parity with omniscient_encode_observation (the T0.1
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
#include <string_view>
#include <type_traits>

#include <gtest/gtest.h>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/knowledge.hpp"
#include "sts/engine/omniscient_observation.hpp"
#include "sts/engine/public_view.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/encounter_table.hpp"
#include "sts/registry/ids.hpp"

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
// v2's size; the v1 prefix's 3760 bytes are pinned by V2TailHasNoImplicitPadding
// asserting that offsetof(PublicView, gold) is still exactly 3760.
static_assert(sizeof(PublicView) == 6032);

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
    // Bounded by where the v1 layout ended: the v2 tail is walked separately by
    // V2TailHasNoImplicitPadding, which also asserts this boundary has not
    // moved (that is the additive-append promise).
    const std::string holes = find_holes<PublicView>(
        members, offsetof(PublicView, gold), "PublicView");
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

    // Player powers -- absent from the OmniscientObsBuffer stub entirely, including the
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
    // (Contrast: the OmniscientObsBuffer stub truncates the same monster at 4 slots --
    // kObsMonsterPowerCap in omniscient_observation.hpp -- which is the gap this closes.)
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

// Neither RNG stream state nor monster construction scratch (pad0 -- the
// Louse's unrevealed bite roll lives there) nor mid-resolution card scratch may
// reach the bytes. These are the genuinely HIDDEN sources.
TEST(PublicView, HiddenStateDoesNotLeak) {
    const RunController a = make_run_in_combat();
    RunController b = make_run_in_combat();

    b.combat.shuffle_rng.counter += 17;   // hidden realization state
    b.combat.ai_rng.s0 ^= 0x12345u;
    b.combat.monsters[0].pad0 = 42;       // unrevealed construction scratch
    b.combat.card_pool[1].misc = 7;       // mid-resolution scratch (excluded)

    PublicView pa{};
    PublicView pb{};
    encode_public_view(a, pa);
    encode_public_view(b, pb);
    EXPECT_EQ(std::memcmp(&pa, &pb, sizeof(PublicView)), 0)
        << "hidden CombatState bytes leaked into PublicView";
}

// The resolution queues are DERIVED, not hidden (audit doc §1): at any decision
// boundary they are a deterministic function of the observed public history, so
// two states whose queues differ are two states with different public
// histories, not a hidden twin. None of their bytes are carried in the view's
// data fields -- but the pending-screen context they imply DOES reach the
// consumer, through the mask channel, which is precisely what the audit's
// "excluded ... the pending-screen context a consumer does need arrives through
// the mask channel" row promises. This test pins both halves.
TEST(PublicView, ResolutionQueuesReachTheViewOnlyThroughTheMaskChannel) {
    const RunController a = make_run_in_combat();
    RunController b = make_run_in_combat();
    b.combat.action_queue[0] = ActionQueueItem{9, 0, 1, 123, 0};
    b.combat.action_count = 1;
    b.combat.action_tail = 1;
    b.combat.turn_has_ended = 1;

    PublicView pa{};
    PublicView pb{};
    encode_public_view(a, pa);
    encode_public_view(b, pb);

    EXPECT_EQ(std::memcmp(&pa, &pb, kPublicViewFixedBytes), 0)
        << "resolution-queue bytes leaked into the view's DATA fields";
    EXPECT_NE(std::memcmp(&pa.action_mask, &pb.action_mask, sizeof(PvMask)), 0)
        << "a pending in-combat resolution left the legal-action mask "
           "unchanged -- the mask channel is supposed to carry exactly this";
}

// --- Runic Dome parity with the omniscient encoder ---------------------------

TEST(PublicView, RunicDomeSuppressionParityWithEncodeObservation) {
    // Without the Dome: both encoders carry the telegraphed intents.
    const RunController plain = make_run_in_combat();
    OmniscientObsBuffer obs_plain{};
    PublicView pv_plain{};
    omniscient_encode_observation(plain.combat, obs_plain);
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
    OmniscientObsBuffer obs_domed{};
    PublicView pv_domed{};
    omniscient_encode_observation(domed.combat, obs_domed);
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
    // pipeline anchors on MonsterState.intent -- omniscient_observation.hpp write-up).
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

    // keys_reserved / act_reserved are POPULATED from v2 on (both name live
    // public RunState fields); make_run_in_combat leaves both RunState fields
    // zero, so they still read zero here. The two genuinely reserved fields
    // below stay zero until S2 lands their screens.
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

// =============================================================================
// T0.2 -- run always-block, per-phase screens, mask channel, knowledge
// =============================================================================

static_assert(sizeof(PvRelic) == 4);
static_assert(sizeof(PvMapNode) == 2);
static_assert(sizeof(PvShopSlot) == 6);
static_assert(sizeof(PvEventBoardCard) == 6);
static_assert(sizeof(PublicView) == 6032);

// --- Layout walk over the v2 element types and the appended tail -------------

TEST(PublicViewLayout, V2ElementTypesHaveNoImplicitPadding) {
    const MemberSpan relic[] = {
        STS_MEMBER_SPAN(PvRelic, relic_id),
        STS_MEMBER_SPAN(PvRelic, counter),
    };
    EXPECT_EQ(find_holes<PvRelic>(relic, sizeof(PvRelic), "PvRelic"), "");

    const MemberSpan node[] = {
        STS_MEMBER_SPAN(PvMapNode, room_type),
        STS_MEMBER_SPAN(PvMapNode, edges),
    };
    EXPECT_EQ(find_holes<PvMapNode>(node, sizeof(PvMapNode), "PvMapNode"), "");

    const MemberSpan item[] = {
        STS_MEMBER_SPAN(PvRewardItem, gold),
        STS_MEMBER_SPAN(PvRewardItem, bonus_gold),
        STS_MEMBER_SPAN(PvRewardItem, id),
        STS_MEMBER_SPAN(PvRewardItem, card_ids),
        STS_MEMBER_SPAN(PvRewardItem, card_upgrades),
        STS_MEMBER_SPAN(PvRewardItem, kind),
        STS_MEMBER_SPAN(PvRewardItem, card_count),
    };
    EXPECT_EQ(find_holes<PvRewardItem>(item, sizeof(PvRewardItem),
                                       "PvRewardItem"), "");

    const MemberSpan reward[] = {
        STS_MEMBER_SPAN(PvReward, items),
        STS_MEMBER_SPAN(PvReward, count),
        STS_MEMBER_SPAN(PvReward, open_card_item),
        STS_MEMBER_SPAN(PvReward, active),
        STS_MEMBER_SPAN(PvReward, pad0),
    };
    EXPECT_EQ(find_holes<PvReward>(reward, sizeof(PvReward), "PvReward"), "");

    const MemberSpan slot[] = {
        STS_MEMBER_SPAN(PvShopSlot, id),
        STS_MEMBER_SPAN(PvShopSlot, price),
        STS_MEMBER_SPAN(PvShopSlot, sold),
        STS_MEMBER_SPAN(PvShopSlot, upgrade),
    };
    EXPECT_EQ(find_holes<PvShopSlot>(slot, sizeof(PvShopSlot), "PvShopSlot"), "");

    const MemberSpan shop[] = {
        STS_MEMBER_SPAN(PvShop, colored),
        STS_MEMBER_SPAN(PvShop, colorless),
        STS_MEMBER_SPAN(PvShop, relics),
        STS_MEMBER_SPAN(PvShop, potions),
        STS_MEMBER_SPAN(PvShop, actual_purge_cost),
        STS_MEMBER_SPAN(PvShop, sale_index),
        STS_MEMBER_SPAN(PvShop, screen),
        STS_MEMBER_SPAN(PvShop, purge_available),
        STS_MEMBER_SPAN(PvShop, active),
    };
    EXPECT_EQ(find_holes<PvShop>(shop, sizeof(PvShop), "PvShop"), "");

    const MemberSpan board[] = {
        STS_MEMBER_SPAN(PvEventBoardCard, card_id),
        STS_MEMBER_SPAN(PvEventBoardCard, upgrade),
        STS_MEMBER_SPAN(PvEventBoardCard, taken),
        STS_MEMBER_SPAN(PvEventBoardCard, revealed),
        STS_MEMBER_SPAN(PvEventBoardCard, pad0),
    };
    EXPECT_EQ(find_holes<PvEventBoardCard>(board, sizeof(PvEventBoardCard),
                                           "PvEventBoardCard"), "");

    const MemberSpan ev[] = {
        STS_MEMBER_SPAN(PvEvent, event_id),
        STS_MEMBER_SPAN(PvEvent, scratch),
        STS_MEMBER_SPAN(PvEvent, screen),
        STS_MEMBER_SPAN(PvEvent, grid_kind),
        STS_MEMBER_SPAN(PvEvent, active),
        STS_MEMBER_SPAN(PvEvent, scratch_public_mask),
        STS_MEMBER_SPAN(PvEvent, board),
    };
    EXPECT_EQ(find_holes<PvEvent>(ev, sizeof(PvEvent), "PvEvent"), "");

    const MemberSpan neow[] = {
        STS_MEMBER_SPAN(PvNeow, hp_bonus),
        STS_MEMBER_SPAN(PvNeow, grid_picked),
        STS_MEMBER_SPAN(PvNeow, option_type),
        STS_MEMBER_SPAN(PvNeow, option_drawback),
        STS_MEMBER_SPAN(PvNeow, screen),
        STS_MEMBER_SPAN(PvNeow, chosen),
        STS_MEMBER_SPAN(PvNeow, grid_mode),
        STS_MEMBER_SPAN(PvNeow, grid_needed),
        STS_MEMBER_SPAN(PvNeow, grid_done),
        STS_MEMBER_SPAN(PvNeow, active),
    };
    EXPECT_EQ(find_holes<PvNeow>(neow, sizeof(PvNeow), "PvNeow"), "");

    const MemberSpan mask[] = {
        STS_MEMBER_SPAN(PvMask, mask),
        STS_MEMBER_SPAN(PvMask, pad_end),
    };
    EXPECT_EQ(find_holes<PvMask>(mask, sizeof(PvMask), "PvMask"), "");
}

TEST(PublicViewLayout, V2TailHasNoImplicitPadding) {
    // The walk starts where the v1 layout ended -- the v1 prefix is proved
    // hole-free by PublicViewHasNoImplicitPadding above, and the ASSERT here is
    // the additive-append promise: no v1 offset moved.
    ASSERT_EQ(offsetof(PublicView, gold), 3760u)
        << "the v2 tail no longer starts where the v1 layout ended -- this is "
           "a BREAKING schema change, not an additive one";

    const MemberSpan members[] = {
        STS_MEMBER_SPAN(PublicView, gold),
        STS_MEMBER_SPAN(PublicView, event_pity_monster),
        STS_MEMBER_SPAN(PublicView, event_pity_shop),
        STS_MEMBER_SPAN(PublicView, event_pity_treasure),
        STS_MEMBER_SPAN(PublicView, event_flags),
        STS_MEMBER_SPAN(PublicView, shop_flags),
        STS_MEMBER_SPAN(PublicView, run_hp),
        STS_MEMBER_SPAN(PublicView, run_max_hp),
        STS_MEMBER_SPAN(PublicView, card_blizz_randomizer),
        STS_MEMBER_SPAN(PublicView, blizzard_potion_mod),
        STS_MEMBER_SPAN(PublicView, purge_cost),
        STS_MEMBER_SPAN(PublicView, floor),
        STS_MEMBER_SPAN(PublicView, master_deck_count),
        STS_MEMBER_SPAN(PublicView, event_membership),
        STS_MEMBER_SPAN(PublicView, special_membership),
        STS_MEMBER_SPAN(PublicView, boss_ids),
        STS_MEMBER_SPAN(PublicView, ascension),
        STS_MEMBER_SPAN(PublicView, shrine_membership),
        STS_MEMBER_SPAN(PublicView, relic_count),
        STS_MEMBER_SPAN(PublicView, cur_x),
        STS_MEMBER_SPAN(PublicView, room_type),
        STS_MEMBER_SPAN(PublicView, combat_outcome),
        STS_MEMBER_SPAN(PublicView, pending_bottle),
        STS_MEMBER_SPAN(PublicView, emerald_x),
        STS_MEMBER_SPAN(PublicView, emerald_y),
        STS_MEMBER_SPAN(PublicView, monster_cursor),
        STS_MEMBER_SPAN(PublicView, elite_cursor),
        STS_MEMBER_SPAN(PublicView, boss_cursor),
        STS_MEMBER_SPAN(PublicView, current_encounter_id),
        STS_MEMBER_SPAN(PublicView, rest_screen),
        STS_MEMBER_SPAN(PublicView, chest_size),
        STS_MEMBER_SPAN(PublicView, chest_relic_tier),
        STS_MEMBER_SPAN(PublicView, chest_has_gold),
        STS_MEMBER_SPAN(PublicView, chest_opened),
        STS_MEMBER_SPAN(PublicView, knowledge_chain_count),
        STS_MEMBER_SPAN(PublicView, knowledge_exact_prefix),
        STS_MEMBER_SPAN(PublicView, knowledge_full_order),
        STS_MEMBER_SPAN(PublicView, monster_roll_known),
        STS_MEMBER_SPAN(PublicView, monster_roll),
        STS_MEMBER_SPAN(PublicView, monster_prefix),
        STS_MEMBER_SPAN(PublicView, elite_prefix),
        STS_MEMBER_SPAN(PublicView, boss_prefix),
        STS_MEMBER_SPAN(PublicView, draw_constraint_rank),
        STS_MEMBER_SPAN(PublicView, draw_exact_pos),
        STS_MEMBER_SPAN(PublicView, master_deck),
        STS_MEMBER_SPAN(PublicView, relics),
        STS_MEMBER_SPAN(PublicView, map),
        STS_MEMBER_SPAN(PublicView, rewards),
        STS_MEMBER_SPAN(PublicView, shop),
        STS_MEMBER_SPAN(PublicView, event),
        STS_MEMBER_SPAN(PublicView, neow),
        STS_MEMBER_SPAN(PublicView, action_mask),
    };
    std::string holes;
    std::size_t cursor = offsetof(PublicView, gold);
    for (const MemberSpan& m : members) {
        if (m.begin != cursor) {
            holes += "\n  [" + std::to_string(cursor) + ", " +
                     std::to_string(m.begin) + ") before PublicView::" + m.name;
        }
        cursor = m.end;
    }
    if (cursor != sizeof(PublicView)) {
        holes += "\n  [" + std::to_string(cursor) + ", " +
                 std::to_string(sizeof(PublicView)) + ") tail";
    }
    EXPECT_TRUE(holes.empty())
        << "PublicView's v2 tail has bytes that belong to no member:" << holes;
}

// --- Sample run states --------------------------------------------------------

// A run OUTSIDE combat, with every always-block source populated: deck, relics,
// map, pity/membership counters, the emerald node and a consumed encounter
// prefix.
RunController make_run_on_map() {
    RunController rc{};
    rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
    rc.cur_x = 3;
    rc.room_type = static_cast<uint8_t>(RoomType::Monster);
    rc.combat_outcome = static_cast<uint8_t>(RunCombatOutcome::KILLED);

    rc.run.hp = 54;
    rc.run.max_hp = 75;
    rc.run.gold = 233;
    rc.run.floor = 7;
    rc.run.ascension = 20;
    rc.run.act = 1;
    rc.run.keys = kKeyRuby;
    rc.run.purge_cost = 100;
    rc.run.card_blizz_randomizer = 3;
    rc.run.blizzard_potion_mod = -10;
    rc.run.event_pity_monster = 0.3f;
    rc.run.event_pity_shop = 0.06f;
    rc.run.event_pity_treasure = 0.04f;
    rc.run.event_flags = 0x5u;
    rc.run.shop_flags = 0x2u;
    rc.run.event_membership = 0x03FFu;
    rc.run.special_membership = 0x1234u;
    rc.run.shrine_membership = 0x3Bu;
    rc.run.boss_ids[0] = 42;
    rc.run.potions[0] = 3;
    rc.run.potion_slots = 2;

    rc.run.master_deck_count = 3;
    rc.run.master_deck[0] =
        CardInstance{static_cast<uint16_t>(CardId::ASCENDERS_BANE), 0, 0, 0, 0};
    rc.run.master_deck[1] =
        CardInstance{static_cast<uint16_t>(CardId::STRIKE), 0, 1, 0, 0};
    rc.run.master_deck[2] =
        CardInstance{static_cast<uint16_t>(CardId::BASH), 1, 2, 0, 0};

    rc.run.relic_count = 2;
    rc.run.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::BURNING_BLOOD), -1};
    rc.run.relics[1] = RelicSlot{static_cast<uint16_t>(RelicId::KUNAI), 2};

    // A couple of map nodes plus the burning-elite marker.
    rc.run.map[0] = MapNode{static_cast<uint8_t>(RoomType::Monster), 0x3};
    rc.run.map[8] = MapNode{static_cast<uint8_t>(RoomType::Elite), 0x1};
    rc.emerald_x = 2;
    rc.emerald_y = 5;

    rc.lists.monster_list[0] = "Cultist";
    rc.lists.monster_list[1] = "Jaw Worm";
    rc.lists.monster_list[2] = "2 Louse";
    rc.lists.monster_list_count = 3;
    rc.lists.elite_list[0] = "Gremlin Nob";
    rc.lists.elite_list_count = 1;
    rc.lists.boss_list[0] = "The Guardian";
    rc.lists.boss_list_count = 1;
    rc.monster_cursor = 2;  // two encounters already fought and left
    return rc;
}

[[nodiscard]] uint8_t encounter_id(std::string_view key) {
    const sts::registry::EncounterDef* def =
        sts::registry::encounter_by_game_id(key);
    return def != nullptr ? def->id : uint8_t{0};
}

// --- The always-block -----------------------------------------------------------

TEST(PublicViewRun, AlwaysBlockScalarsRoundTrip) {
    const RunController rc = make_run_on_map();
    PublicView pv{};
    encode_public_view(rc, pv);

    EXPECT_EQ(pv.public_view_version, 2u);
    EXPECT_EQ(pv.run_phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(pv.combat_active, 0);

    EXPECT_EQ(pv.run_hp, 54);
    EXPECT_EQ(pv.run_max_hp, 75);
    EXPECT_EQ(pv.gold, 233);
    EXPECT_EQ(pv.floor, 7u);
    EXPECT_EQ(pv.ascension, 20);
    EXPECT_EQ(pv.cur_x, 3);
    EXPECT_EQ(pv.room_type, static_cast<uint8_t>(RoomType::Monster));
    EXPECT_EQ(pv.combat_outcome, static_cast<uint8_t>(RunCombatOutcome::KILLED));

    // Pity + membership: plan §1 tracker state, encoded verbatim.
    EXPECT_EQ(pv.card_blizz_randomizer, 3);
    EXPECT_EQ(pv.blizzard_potion_mod, -10);
    EXPECT_FLOAT_EQ(pv.event_pity_monster, 0.3f);
    EXPECT_FLOAT_EQ(pv.event_pity_shop, 0.06f);
    EXPECT_FLOAT_EQ(pv.event_pity_treasure, 0.04f);
    EXPECT_EQ(pv.event_membership, 0x03FFu);
    EXPECT_EQ(pv.special_membership, 0x1234u);
    EXPECT_EQ(pv.shrine_membership, 0x3Bu);
    EXPECT_EQ(pv.event_flags, 0x5u);
    EXPECT_EQ(pv.shop_flags, 0x2u);
    EXPECT_EQ(pv.purge_cost, 100);
    EXPECT_EQ(pv.boss_ids[0], 42u);

    // The two reserved fields v2 populates with their declared meanings.
    EXPECT_EQ(pv.keys_reserved, kKeyRuby);
    EXPECT_EQ(pv.act_reserved, 1);
}

TEST(PublicViewRun, MasterDeckIsCarriedInEngineOrder) {
    const RunController rc = make_run_on_map();
    PublicView pv{};
    encode_public_view(rc, pv);

    // Engine order, NOT sorted: this index space is the one the mask's
    // can_choose_master_deck[] addresses, so re-ordering it here would
    // desynchronize the observation from the action space.
    ASSERT_EQ(pv.master_deck_count, 3u);
    EXPECT_EQ(pv.master_deck[0].card_id,
              static_cast<uint16_t>(CardId::ASCENDERS_BANE));
    EXPECT_EQ(pv.master_deck[1].card_id, static_cast<uint16_t>(CardId::STRIKE));
    EXPECT_EQ(pv.master_deck[2].card_id, static_cast<uint16_t>(CardId::BASH));
    EXPECT_EQ(pv.master_deck[2].upgrade, 1);
    EXPECT_EQ(pv.master_deck[3].card_id, 0u);
}

TEST(PublicViewRun, RelicsMapAndEmeraldNodeRoundTrip) {
    const RunController rc = make_run_on_map();
    PublicView pv{};
    encode_public_view(rc, pv);

    ASSERT_EQ(pv.relic_count, 2);
    EXPECT_EQ(pv.relics[0].relic_id,
              static_cast<uint16_t>(RelicId::BURNING_BLOOD));
    EXPECT_EQ(pv.relics[0].counter, -1);
    EXPECT_EQ(pv.relics[1].relic_id, static_cast<uint16_t>(RelicId::KUNAI));
    EXPECT_EQ(pv.relics[1].counter, 2);  // the DISPLAYED counter

    EXPECT_EQ(pv.map[0].room_type, static_cast<uint8_t>(RoomType::Monster));
    EXPECT_EQ(pv.map[0].edges, 0x3);
    EXPECT_EQ(pv.map[8].room_type, static_cast<uint8_t>(RoomType::Elite));
    EXPECT_EQ(pv.map[1].room_type, 0);
    EXPECT_EQ(pv.emerald_x, 2);
    EXPECT_EQ(pv.emerald_y, 5);
}

// The combat relic MIRROR is the source while a combat is live: in-combat
// counter ticks land there and only fold back at combat end, so reading
// RunState there would show a number the screen does not.
TEST(PublicViewRun, RelicCountersComeFromTheCombatMirrorInCombat) {
    RunController rc = make_run_in_combat();
    rc.run.relic_count = 1;
    rc.run.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::KUNAI), 0};
    rc.combat.relic_count = 1;
    rc.combat.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::KUNAI), 2};

    PublicView pv{};
    encode_public_view(rc, pv);
    ASSERT_EQ(pv.relic_count, 1);
    EXPECT_EQ(pv.relics[0].counter, 2) << "the pre-fold-back RunState counter "
                                          "reached the view instead of the "
                                          "live mirror";

    rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
    encode_public_view(rc, pv);
    EXPECT_EQ(pv.relics[0].counter, 0) << "outside combat the mirror is stale";
}

TEST(PublicViewRun, ConsumedEncounterPrefixOnlyCarriesWhatWasFought) {
    const RunController rc = make_run_on_map();
    PublicView pv{};
    encode_public_view(rc, pv);

    EXPECT_EQ(pv.monster_cursor, 2);
    EXPECT_EQ(pv.monster_prefix[0], encounter_id("Cultist"));
    EXPECT_EQ(pv.monster_prefix[1], encounter_id("Jaw Worm"));
    // The UNCONSUMED suffix is a monsterRng realization: it must not appear.
    EXPECT_EQ(pv.monster_prefix[2], 0)
        << "the unfought encounter-list suffix leaked into PublicView";
    EXPECT_EQ(pv.elite_cursor, 0);
    EXPECT_EQ(pv.elite_prefix[0], 0);
    // The act boss is public from the map screen regardless of the cursor.
    EXPECT_EQ(pv.boss_prefix[0], encounter_id("The Guardian"));
    EXPECT_EQ(pv.current_encounter_id, 0) << "not inside a room";
}

TEST(PublicViewRun, CurrentEncounterIsPublicWhileInsideTheRoom) {
    RunController rc = make_run_on_map();
    rc.phase = static_cast<uint8_t>(RunPhase::COMBAT);
    rc.room_type = static_cast<uint8_t>(RoomType::Monster);
    PublicView pv{};
    encode_public_view(rc, pv);
    EXPECT_EQ(pv.current_encounter_id, encounter_id("2 Louse"));
}

// The unconsumed encounter suffix is hidden: permuting it must not move a byte.
TEST(PublicViewRun, EncounterSuffixDoesNotLeak) {
    const RunController a = make_run_on_map();
    RunController b = make_run_on_map();
    b.lists.monster_list[2] = "Gremlin Gang";
    b.lists.monster_list[3] = "Blue Slaver";
    b.lists.monster_list_count = 4;

    PublicView pa{};
    PublicView pb{};
    encode_public_view(a, pa);
    encode_public_view(b, pb);
    EXPECT_EQ(std::memcmp(&pa, &pb, sizeof(PublicView)), 0)
        << "the unconsumed encounter-list suffix leaked into PublicView";
}

// --- The mask channel ------------------------------------------------------------

// The RunActionMask bytes are part of the record (plan §2.1), so a legality bit
// derived from hidden state falls under the same twin tests as everything else.
TEST(PublicViewRun, MaskChannelMatchesLegalActions) {
    const RunController rc = make_run_on_map();
    PublicView pv{};
    encode_public_view(rc, pv);

    RunActionMask expected{};
    legal_actions(rc, expected);
    EXPECT_EQ(std::memcmp(&pv.action_mask.mask, &expected, sizeof(RunActionMask)),
              0)
        << "the embedded mask channel disagrees with legal_actions()";
    EXPECT_EQ(pv.action_mask.mask.phase, rc.phase);
    for (std::size_t i = 0; i < kPvMaskPad; ++i) {
        EXPECT_EQ(pv.action_mask.pad_end[i], 0) << "pad byte " << i;
    }
}

// --- Per-phase screens ------------------------------------------------------------

TEST(PublicViewScreens, RewardScreenContentsAppear) {
    RunController rc = make_run_on_map();
    rc.phase = static_cast<uint8_t>(RunPhase::COMBAT_REWARD);
    rc.rewards.count = 3;
    rc.rewards.open_card_item = 2;
    rc.rewards.items[0].kind = static_cast<uint8_t>(RewardItemKind::GOLD);
    rc.rewards.items[0].gold = 27;
    rc.rewards.items[0].bonus_gold = 6;
    rc.rewards.items[1].kind = static_cast<uint8_t>(RewardItemKind::RELIC);
    rc.rewards.items[1].id = static_cast<uint16_t>(RelicId::KUNAI);
    rc.rewards.items[2].kind = static_cast<uint8_t>(RewardItemKind::CARDS);
    rc.rewards.items[2].card_count = 3;
    rc.rewards.items[2].card_ids[0] = static_cast<uint16_t>(CardId::BASH);
    rc.rewards.items[2].card_ids[1] = static_cast<uint16_t>(CardId::STRIKE);
    rc.rewards.items[2].card_ids[2] = static_cast<uint16_t>(CardId::DEFEND);
    rc.rewards.items[2].card_upgrades[1] = 1;

    PublicView pv{};
    encode_public_view(rc, pv);

    ASSERT_EQ(pv.rewards.active, 1);
    EXPECT_EQ(pv.rewards.count, 3);
    EXPECT_EQ(pv.rewards.open_card_item, 2);
    EXPECT_EQ(pv.rewards.items[0].kind,
              static_cast<uint8_t>(RewardItemKind::GOLD));
    EXPECT_EQ(pv.rewards.items[0].gold, 27);
    EXPECT_EQ(pv.rewards.items[0].bonus_gold, 6);
    EXPECT_EQ(pv.rewards.items[1].id, static_cast<uint16_t>(RelicId::KUNAI));
    EXPECT_EQ(pv.rewards.items[2].card_count, 3);
    EXPECT_EQ(pv.rewards.items[2].card_ids[2],
              static_cast<uint16_t>(CardId::DEFEND));
    EXPECT_EQ(pv.rewards.items[2].card_upgrades[1], 1);
}

// The gate is the SCREEN, not the struct: Dead Adventurer stocks rc.rewards
// with the rewards the player never searched out and THEN starts a combat, so
// a struct-emptiness gate would hand the agent unrevealed rolls.
TEST(PublicViewScreens, RewardScreenIsNotEncodedWhileItIsNotOnScreen) {
    RunController rc = make_run_on_map();
    rc.phase = static_cast<uint8_t>(RunPhase::COMBAT);
    rc.rewards.count = 1;
    rc.rewards.items[0].kind = static_cast<uint8_t>(RewardItemKind::RELIC);
    rc.rewards.items[0].id = static_cast<uint16_t>(RelicId::KUNAI);

    PublicView pv{};
    encode_public_view(rc, pv);
    EXPECT_EQ(pv.rewards.active, 0);
    EXPECT_EQ(pv.rewards.count, 0);
    EXPECT_EQ(pv.rewards.items[0].id, 0u)
        << "a reward row the player has not seen leaked into PublicView";
}

TEST(PublicViewScreens, ShopStockAppears) {
    RunController rc = make_run_on_map();
    rc.phase = static_cast<uint8_t>(RunPhase::SHOP);
    rc.shop.screen = static_cast<uint8_t>(ShopScreenKind::MENU);
    rc.shop.sale_index = 2;
    rc.shop.purge_available = 1;
    rc.shop.actual_purge_cost = 75;
    rc.shop.colored[0] = ShopSlot{static_cast<uint16_t>(CardId::BASH), 120, 0, 1, {0, 0}};
    rc.shop.colored[2] = ShopSlot{static_cast<uint16_t>(CardId::STRIKE), 45, 1, 0, {0, 0}};
    rc.shop.relics[1] = ShopSlot{static_cast<uint16_t>(RelicId::KUNAI), 180, 0, 0, {0, 0}};
    rc.shop.potions[0] = ShopSlot{7, 60, 0, 0, {0, 0}};

    PublicView pv{};
    encode_public_view(rc, pv);

    ASSERT_EQ(pv.shop.active, 1);
    EXPECT_EQ(pv.shop.screen, static_cast<uint8_t>(ShopScreenKind::MENU));
    EXPECT_EQ(pv.shop.sale_index, 2);
    EXPECT_EQ(pv.shop.purge_available, 1);
    EXPECT_EQ(pv.shop.actual_purge_cost, 75);
    EXPECT_EQ(pv.shop.colored[0].id, static_cast<uint16_t>(CardId::BASH));
    EXPECT_EQ(pv.shop.colored[0].price, 120);
    EXPECT_EQ(pv.shop.colored[0].upgrade, 1);
    EXPECT_EQ(pv.shop.colored[2].sold, 1);
    EXPECT_EQ(pv.shop.relics[1].id, static_cast<uint16_t>(RelicId::KUNAI));
    EXPECT_EQ(pv.shop.potions[0].price, 60);

    // Left the shop: the stale stock must not follow the player onto the map.
    rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
    encode_public_view(rc, pv);
    EXPECT_EQ(pv.shop.active, 0);
    EXPECT_EQ(pv.shop.colored[0].id, 0u);
}

TEST(PublicViewScreens, NeowBlessingOptionsAppear) {
    RunController rc = make_run_on_map();
    rc.phase = static_cast<uint8_t>(RunPhase::NEOW);
    rc.cur_x = kNeowColumn;
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::BLESSING);
    rc.neow.chosen = kNeowNoChoice;
    rc.neow.hp_bonus = 8;
    rc.neow.option_type[0] = static_cast<uint8_t>(NeowRewardType::THREE_CARDS);
    rc.neow.option_type[1] = static_cast<uint8_t>(NeowRewardType::REMOVE_CARD);
    rc.neow.option_type[2] = static_cast<uint8_t>(NeowRewardType::HUNDRED_GOLD);
    rc.neow.option_type[3] = static_cast<uint8_t>(NeowRewardType::BOSS_RELIC);
    rc.neow.option_drawback[2] = static_cast<uint8_t>(NeowDrawback::CURSE);
    rc.neow.grid_picked[0] = 4;
    rc.neow.grid_needed = 1;

    PublicView pv{};
    encode_public_view(rc, pv);

    ASSERT_EQ(pv.neow.active, 1);
    EXPECT_EQ(pv.neow.screen, static_cast<uint8_t>(NeowScreen::BLESSING));
    EXPECT_EQ(pv.neow.chosen, kNeowNoChoice);
    EXPECT_EQ(pv.neow.hp_bonus, 8);
    EXPECT_EQ(pv.neow.option_type[3],
              static_cast<uint8_t>(NeowRewardType::BOSS_RELIC));
    EXPECT_EQ(pv.neow.option_drawback[2],
              static_cast<uint8_t>(NeowDrawback::CURSE));
    EXPECT_EQ(pv.neow.grid_picked[0], 4u);
    EXPECT_EQ(pv.neow.grid_needed, 1);
}

TEST(PublicViewScreens, RestSiteScreenAppears) {
    RunController rc = make_run_on_map();
    rc.phase = static_cast<uint8_t>(RunPhase::REST_SITE);
    rc.rest.screen = static_cast<uint8_t>(RestScreen::SMITH);

    PublicView pv{};
    encode_public_view(rc, pv);
    EXPECT_EQ(pv.rest_screen, static_cast<uint8_t>(RestScreen::SMITH));
    // The campfire's option list is rebuilt per call rather than stored, so it
    // reaches the view through the mask channel -- which is exactly the case
    // plan §2.1 gives that channel for.
    EXPECT_EQ(pv.action_mask.mask.phase,
              static_cast<uint8_t>(RunPhase::REST_SITE));

    rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
    encode_public_view(rc, pv);
    EXPECT_EQ(pv.rest_screen, 0);
}

TEST(PublicViewScreens, EventDialogAppearsWithPublicScratch) {
    RunController rc = make_run_on_map();
    rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
    rc.room_type = static_cast<uint8_t>(RoomType::Event);
    rc.event.event_id = static_cast<uint16_t>(sts::registry::EventId::SCRAP_OOZE);
    rc.event.screen = 1;
    rc.event.grid_kind = static_cast<uint8_t>(EventGridKind::NONE);
    rc.event.scratch0 = 35;  // the DISPLAYED dig chance
    rc.event.scratch1 = 5;   // the displayed damage

    PublicView pv{};
    encode_public_view(rc, pv);

    ASSERT_EQ(pv.event.active, 1);
    EXPECT_EQ(pv.event.event_id,
              static_cast<uint16_t>(sts::registry::EventId::SCRAP_OOZE));
    EXPECT_EQ(pv.event.screen, 1);
    EXPECT_EQ(pv.event.scratch_public_mask, 0x0F);
    EXPECT_EQ(pv.event.scratch[0], 35);
    EXPECT_EQ(pv.event.scratch[1], 5);
}

// A ROOM_UNIMPLEMENTED stall keeps the selected EventId observable -- but only
// when the stall IS the event room. An unimplemented ENCOUNTER parks in the
// same phase, and rc.event may then still hold a previous room's dialog.
TEST(PublicViewScreens, UnimplementedRoomCarriesTheEventIdOnlyForAnEventRoom) {
    RunController rc = make_run_on_map();
    rc.phase = static_cast<uint8_t>(RunPhase::ROOM_UNIMPLEMENTED);
    rc.room_type = static_cast<uint8_t>(RoomType::Event);
    rc.event.event_id = static_cast<uint16_t>(sts::registry::EventId::NLOTH);

    PublicView pv{};
    encode_public_view(rc, pv);
    EXPECT_EQ(pv.event.active, 1);
    EXPECT_EQ(pv.event.event_id,
              static_cast<uint16_t>(sts::registry::EventId::NLOTH));

    rc.room_type = static_cast<uint8_t>(RoomType::Elite);  // an unmodelled fight
    encode_public_view(rc, pv);
    EXPECT_EQ(pv.event.active, 0);
    EXPECT_EQ(pv.event.event_id, 0u)
        << "a previous room's finished dialog was reported as the live screen";
}

// Dead Adventurer's scratch is the one hidden row: scratch0 packs a miscRng
// shuffle of the three search rewards and scratch1 names the elite it will
// spring. Neither is on screen.
TEST(PublicViewScreens, DeadAdventurerScratchIsMasked) {
    RunController a = make_run_on_map();
    a.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
    a.event.event_id =
        static_cast<uint16_t>(sts::registry::EventId::DEAD_ADVENTURER);
    a.event.scratch0 = 0x0026;  // one packed reward order
    a.event.scratch1 = 1;       // Gremlin Nob
    RunController b = a;
    b.event.scratch0 = 0x0011;  // a different order
    b.event.scratch1 = 2;       // Lagavulin

    PublicView pa{};
    PublicView pb{};
    encode_public_view(a, pa);
    encode_public_view(b, pb);

    EXPECT_EQ(pa.event.scratch_public_mask, 0);
    EXPECT_EQ(pa.event.scratch[0], 0);
    EXPECT_EQ(pa.event.scratch[1], 0);
    EXPECT_EQ(pa.event.event_id,
              static_cast<uint16_t>(sts::registry::EventId::DEAD_ADVENTURER));
    EXPECT_EQ(std::memcmp(&pa, &pb, sizeof(PublicView)), 0)
        << "Dead Adventurer's unrevealed rolls leaked into PublicView";
}

// Match and Keep deals twelve FACE-DOWN cards: only the flipped slot and the
// matched-away pairs have public identities.
TEST(PublicViewScreens, MatchAndKeepBoardIsMaskedUntilFlipped) {
    RunController rc = make_run_on_map();
    rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
    rc.event.event_id =
        static_cast<uint16_t>(sts::registry::EventId::MATCH_AND_KEEP);
    rc.event.screen = 2;
    rc.event.scratch0 = 4;  // attempts left
    rc.event.scratch1 = 3;  // slot 3 is currently face up
    for (int i = 0; i < kEventBoardCap; ++i) {
        rc.event.board[i].card_id = static_cast<uint16_t>(CardId::STRIKE);
    }
    rc.event.board[3].card_id = static_cast<uint16_t>(CardId::BASH);
    rc.event.board[7].card_id = static_cast<uint16_t>(CardId::DEFEND);
    rc.event.board[7].taken = 1;
    rc.event.board[9].card_id = static_cast<uint16_t>(CardId::DEFEND);
    rc.event.board[9].taken = 1;

    PublicView pv{};
    encode_public_view(rc, pv);

    EXPECT_EQ(pv.event.scratch[0], 4);
    EXPECT_EQ(pv.event.scratch[1], 3);
    // Flipped: public.
    EXPECT_EQ(pv.event.board[3].revealed, 1);
    EXPECT_EQ(pv.event.board[3].card_id, static_cast<uint16_t>(CardId::BASH));
    // Matched away: public.
    EXPECT_EQ(pv.event.board[7].revealed, 1);
    EXPECT_EQ(pv.event.board[7].taken, 1);
    EXPECT_EQ(pv.event.board[7].card_id, static_cast<uint16_t>(CardId::DEFEND));
    // Still face down: masked, but the slot's existence is not.
    EXPECT_EQ(pv.event.board[0].revealed, 0);
    EXPECT_EQ(pv.event.board[0].card_id, 0u)
        << "a face-down Match-and-Keep card leaked into PublicView";
    EXPECT_EQ(pv.event.board[0].taken, 0);
}

// --- The treasure chest (plan §2.1's named masking trap) --------------------------

RunController make_run_at_chest() {
    RunController rc = make_run_on_map();
    rc.phase = static_cast<uint8_t>(RunPhase::TREASURE_ROOM);
    rc.room_type = static_cast<uint8_t>(RoomType::Treasure);
    rc.treasure_chest = TreasureChest{static_cast<uint8_t>(ChestSize::MEDIUM),
                                      static_cast<uint8_t>(RelicTier::RARE),
                                      /*has_gold=*/1, /*opened=*/0};
    return rc;
}

TEST(PublicViewChest, ContentsAreAbsentPreOpenAndPresentPostOpen) {
    RunController rc = make_run_at_chest();
    PublicView pv{};
    encode_public_view(rc, pv);

    // Pre-open: the SIZE is drawn on the room, the contents are a
    // construction-time roll the player has not seen.
    EXPECT_EQ(pv.chest_size, static_cast<uint8_t>(ChestSize::MEDIUM));
    EXPECT_EQ(pv.chest_opened, 0);
    EXPECT_EQ(pv.chest_relic_tier, 0)
        << "the unopened chest's relic tier leaked into PublicView";
    EXPECT_EQ(pv.chest_has_gold, 0)
        << "the unopened chest's gold roll leaked into PublicView";

    // Post-open: the same bytes are now on screen.
    rc.treasure_chest.opened = 1;
    encode_public_view(rc, pv);
    EXPECT_EQ(pv.chest_size, static_cast<uint8_t>(ChestSize::MEDIUM));
    EXPECT_EQ(pv.chest_opened, 1);
    EXPECT_EQ(pv.chest_relic_tier, static_cast<uint8_t>(RelicTier::RARE));
    EXPECT_EQ(pv.chest_has_gold, 1);
}

// The reveal-timing property in its byte form (plan §2.6c): two states that
// differ ONLY in unopened chest contents serialize identically.
TEST(PublicViewChest, ContentsDoNotLeakBeforeTheOpenAction) {
    const RunController a = make_run_at_chest();
    RunController b = make_run_at_chest();
    b.treasure_chest.relic_tier = static_cast<uint8_t>(RelicTier::COMMON);
    b.treasure_chest.has_gold = 0;

    PublicView pa{};
    PublicView pb{};
    encode_public_view(a, pa);
    encode_public_view(b, pb);
    EXPECT_EQ(std::memcmp(&pa, &pb, sizeof(PublicView)), 0)
        << "unopened treasure-chest contents leaked into the serialized view";

    // ... and once opened they DO differ: the masking is a reveal gate, not a
    // permanent exclusion.
    RunController a2 = a;
    RunController b2 = b;
    a2.treasure_chest.opened = 1;
    b2.treasure_chest.opened = 1;
    encode_public_view(a2, pa);
    encode_public_view(b2, pb);
    EXPECT_NE(std::memcmp(&pa, &pb, sizeof(PublicView)), 0)
        << "opened chest contents are public and must be carried";
}

TEST(PublicViewChest, ChestIsNotEncodedAwayFromTheRoom) {
    RunController rc = make_run_at_chest();
    rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
    PublicView pv{};
    encode_public_view(rc, pv);
    EXPECT_EQ(pv.chest_size, 0);
    EXPECT_EQ(pv.chest_opened, 0);
}

// --- KnowledgeState projection ----------------------------------------------------

// Headbutt's discard-to-top: the placed card is known to be the top card, and
// that survives into the view as an exact from-top position -- WITHOUT the draw
// pile leaving its canonical sorted order.
TEST(PublicViewKnowledge, HeadbuttKnownTopCardRoundTrips) {
    RunController rc = make_run_in_combat();
    // Place pool row 4 (Bash+) on top of the pile (piles.hpp: the top is the
    // LAST array entry), then fire the placement hook the interpreter fires.
    rc.combat.draw[rc.combat.draw_count] = 4;
    rc.combat.draw_count = static_cast<uint8_t>(rc.combat.draw_count + 1);
    {
        KnowledgeScope scope(&rc.knowledge);
        knowledge_on_place_top(rc.combat, 4);
    }
    ASSERT_EQ(rc.knowledge.chain_count, 1);
    ASSERT_EQ(rc.knowledge.exact_prefix, 1);

    PublicView pv{};
    encode_public_view(rc, pv);

    EXPECT_EQ(pv.knowledge_chain_count, 1);
    EXPECT_EQ(pv.knowledge_exact_prefix, 1);
    EXPECT_EQ(pv.knowledge_full_order, 0);

    int annotated = 0;
    int known_slot = -1;
    for (int i = 0; i < pv.draw_count; ++i) {
        if (pv.draw_constraint_rank[i] != 0) {
            ++annotated;
            known_slot = i;
            EXPECT_EQ(pv.draw_constraint_rank[i], 1);
            EXPECT_EQ(pv.draw_exact_pos[i], 1) << "slot " << i;
        } else {
            EXPECT_EQ(pv.draw_exact_pos[i], 0) << "slot " << i;
        }
    }
    ASSERT_EQ(annotated, 1);
    EXPECT_EQ(pv.draw[known_slot].card_id, static_cast<uint16_t>(CardId::BASH));
    EXPECT_EQ(pv.draw[known_slot].upgrade, 1);
}

// Frozen Eye (REVEAL_DRAW_ORDER): the whole pile is known in order, and the
// exact positions reconstruct that order out of the sorted multiset.
TEST(PublicViewKnowledge, FrozenEyeFullOrderRoundTrips) {
    RunController rc = make_run_in_combat();
    rc.combat.relics[0] =
        RelicSlot{static_cast<uint16_t>(RelicId::FROZEN_EYE), -1};
    rc.combat.relic_count = 1;
    {
        KnowledgeScope scope(&rc.knowledge);
        knowledge_on_shuffle(rc.combat);
    }
    ASSERT_EQ(rc.knowledge.full_order, 1);
    ASSERT_EQ(rc.knowledge.chain_count, rc.combat.draw_count);

    PublicView pv{};
    encode_public_view(rc, pv);

    EXPECT_EQ(pv.knowledge_full_order, 1);
    EXPECT_EQ(pv.knowledge_chain_count, pv.draw_count);
    EXPECT_EQ(pv.knowledge_exact_prefix, pv.draw_count);

    // Every slot carries an exact position, and reading the sorted pile back
    // through those positions yields the TRUE top-first order.
    for (int pos = 1; pos <= pv.draw_count; ++pos) {
        int matches = 0;
        PvCard at{};
        for (int i = 0; i < pv.draw_count; ++i) {
            if (pv.draw_exact_pos[i] == pos) {
                ++matches;
                at = pv.draw[i];
            }
        }
        ASSERT_EQ(matches, 1) << "exact position " << pos
                              << " is not held by exactly one slot";
        const CardInstance& truth =
            rc.combat.card_pool[rc.combat.draw[rc.combat.draw_count - pos]];
        EXPECT_EQ(at.card_id, truth.card_id) << "position " << pos;
        EXPECT_EQ(at.upgrade, truth.upgrade) << "position " << pos;
    }
}

// Revealed monster construction rolls arrive through this projection, never by
// leaking MonsterState.pad0 (which stays excluded wholesale).
TEST(PublicViewKnowledge, RevealedMonsterRollsAppearAndUnrevealedOnesDoNot) {
    RunController rc = make_run_in_combat();
    rc.knowledge.monster_roll_known[0] = 1;
    rc.knowledge.monster_roll[0] = 6;
    rc.knowledge.monster_roll[1] = 7;  // recorded but NOT revealed

    PublicView pv{};
    encode_public_view(rc, pv);
    EXPECT_EQ(pv.monster_roll_known[0], 1);
    EXPECT_EQ(pv.monster_roll[0], 6);
    EXPECT_EQ(pv.monster_roll_known[1], 0);
    EXPECT_EQ(pv.monster_roll[1], 0)
        << "an unrevealed construction roll leaked into PublicView";
}

TEST(PublicViewKnowledge, KnowledgeIsNotEncodedOutsideCombat) {
    RunController rc = make_run_in_combat();
    {
        KnowledgeScope scope(&rc.knowledge);
        knowledge_on_place_top(rc.combat, rc.combat.draw[rc.combat.draw_count - 1]);
    }
    rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
    PublicView pv{};
    encode_public_view(rc, pv);
    EXPECT_EQ(pv.knowledge_chain_count, 0);
    EXPECT_EQ(pv.draw_constraint_rank[0], 0);
}

}  // namespace
}  // namespace sts::engine
