// A5.2 acceptance suite (Stage A ledger): observation encoder stub.
//
//   * Round-trip spot checks -- construct a CombatState with hand-picked
//     non-default values, encode_observation(), assert the ObsBuffer fields
//     match exactly (player, hand cards+costs, monster hp/intent/powers), plus
//     the padding/sentinel behavior for unused hand and monster slots.
//   * Zero allocation -- encode_observation() performs no heap allocation. The
//     acceptance line ("asan + counting allocator in test") is met by a global
//     operator new/delete override IN THIS TRANSLATION UNIT ONLY that increments
//     a counter, snapshotted tightly around just the call under test. (asan
//     alone does not fail on "an allocation happened" -- it only catches memory
//     errors -- so the counting allocator is the real check; asan runs it too
//     via the asan preset for memory-safety coverage.)
//
// The allocation-counting overrides deliberately live here, not in any engine
// header/library TU, so they never pollute code that links sts_engine.

#include <cstddef>
#include <cstdlib>
#include <new>
#include <type_traits>

#include <gtest/gtest.h>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/observation.hpp"
#include "sts/engine/types.hpp"

// --- Global allocation counter (this TU only) -------------------------------
//
// A plain global counter bumped by every replaceable global operator new form.
// We never assert on the process-wide count (GoogleTest and libstdc++ allocate
// freely); we snapshot before/after a TIGHT scope around encode_observation().

namespace {
volatile std::size_t g_alloc_count = 0;
}

// All replaceable global forms are overridden and routed through the SAME
// malloc/free pair so there is no path where an allocation goes through the
// C++ runtime's (or AddressSanitizer's) operator new while its matching free
// goes through ours -- that inconsistency is what triggers asan's
// alloc-dealloc-mismatch (e.g. libstdc++'s std::get_temporary_buffer uses the
// nothrow new form, so that form must be counted/handled here too).
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

// --- Regression-guard static asserts (mirror the header source of truth) ----

static_assert(std::is_trivially_copyable_v<ObsBuffer>);
static_assert(sizeof(ObsBuffer) == 188);

// Build a CombatState with a small, fully-known combat situation.
CombatState make_sample_state() {
    CombatState s{};
    s.player_hp = 68;
    s.player_max_hp = 80;
    s.player_block = 12;
    s.player_energy = 3;

    // Card pool: two known cards referenced by the hand. Bash: cost 2. Defend:
    // cost 1. Pool indices are arbitrary; the hand holds indices into the pool.
    s.card_pool[0] = CardInstance{static_cast<uint16_t>(CardId::BASH), 0, 2, 0, 0};
    s.card_pool[1] = CardInstance{static_cast<uint16_t>(CardId::DEFEND), 0, 1, 0, 0};
    s.hand[0] = 0;  // -> Bash
    s.hand[1] = 1;  // -> Defend
    s.hand_count = 2;

    // One monster: Jaw Worm at 30/44, intent 7, with Strength 3 and Vulnerable 2.
    s.monster_count = 1;
    MonsterState& jw = s.monsters[0];
    jw.monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    jw.hp = 30;
    jw.max_hp = 44;
    jw.intent = 7;
    jw.power_count = 2;
    jw.powers[0] = PowerSlot{static_cast<uint16_t>(PowerId::STRENGTH), 3};
    jw.powers[1] = PowerSlot{static_cast<uint16_t>(PowerId::VULNERABLE), 2};
    return s;
}

// --- Round-trip spot checks -------------------------------------------------

TEST(Observation, PlayerFieldsRoundTrip) {
    const CombatState s = make_sample_state();
    ObsBuffer obs{};
    encode_observation(s, obs);

    EXPECT_EQ(obs.schema_version, SCHEMA_VERSION);
    EXPECT_EQ(obs.player_hp, 68);
    EXPECT_EQ(obs.player_max_hp, 80);
    EXPECT_EQ(obs.player_block, 12);
    EXPECT_EQ(obs.player_energy, 3);
}

TEST(Observation, HandCardsRoundTrip) {
    const CombatState s = make_sample_state();
    ObsBuffer obs{};
    encode_observation(s, obs);

    EXPECT_EQ(obs.hand_count, 2u);
    EXPECT_EQ(obs.hand_card_id[0], static_cast<uint16_t>(CardId::BASH));
    EXPECT_EQ(obs.hand_cost[0], 2);
    EXPECT_EQ(obs.hand_card_id[1], static_cast<uint16_t>(CardId::DEFEND));
    EXPECT_EQ(obs.hand_cost[1], 1);
}

TEST(Observation, UnusedHandSlotsReadSentinels) {
    const CombatState s = make_sample_state();  // only 2 of 10 hand slots filled
    ObsBuffer obs{};
    encode_observation(s, obs);

    for (int i = 2; i < kObsHandCap; ++i) {
        EXPECT_EQ(obs.hand_card_id[i], kObsEmptyCardId) << "hand slot " << i;
        EXPECT_EQ(obs.hand_cost[i], kObsEmptyCost) << "hand slot " << i;
    }
}

TEST(Observation, MonsterFieldsRoundTrip) {
    const CombatState s = make_sample_state();
    ObsBuffer obs{};
    encode_observation(s, obs);

    EXPECT_EQ(obs.monster_count, 1u);
    const ObsMonster& m = obs.monsters[0];
    EXPECT_EQ(m.occupied, 1);
    EXPECT_EQ(m.monster_id, static_cast<uint16_t>(MonsterId::JAW_WORM));
    EXPECT_EQ(m.hp, 30);
    EXPECT_EQ(m.max_hp, 44);
    EXPECT_EQ(m.intent, 7);
    EXPECT_EQ(m.power_count, 2);
    EXPECT_EQ(m.powers[0].power_id, static_cast<uint16_t>(PowerId::STRENGTH));
    EXPECT_EQ(m.powers[0].amount, 3);
    EXPECT_EQ(m.powers[1].power_id, static_cast<uint16_t>(PowerId::VULNERABLE));
    EXPECT_EQ(m.powers[1].amount, 2);
    // Unused per-monster power slots read PowerId::NONE / 0.
    for (int p = 2; p < kObsMonsterPowerCap; ++p) {
        EXPECT_EQ(m.powers[p].power_id, 0u) << "power slot " << p;
        EXPECT_EQ(m.powers[p].amount, 0) << "power slot " << p;
    }
}

TEST(Observation, UnusedMonsterSlotsReadEmpty) {
    const CombatState s = make_sample_state();  // only 1 of 5 monster slots
    ObsBuffer obs{};
    encode_observation(s, obs);

    for (int m = 1; m < kObsMonsterCap; ++m) {
        const ObsMonster& om = obs.monsters[m];
        EXPECT_EQ(om.occupied, 0) << "monster slot " << m;
        EXPECT_EQ(om.monster_id, 0u) << "monster slot " << m;
        EXPECT_EQ(om.hp, 0) << "monster slot " << m;
        EXPECT_EQ(om.max_hp, 0) << "monster slot " << m;
        EXPECT_EQ(om.power_count, 0) << "monster slot " << m;
    }
}

// A monster with more live powers than kObsMonsterPowerCap is truncated in the
// observation, but power_count still reports the true count so a consumer can
// detect the truncation.
TEST(Observation, MonsterPowersTruncateButReportTrueCount) {
    CombatState s{};
    s.monster_count = 1;
    MonsterState& m = s.monsters[0];
    m.monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    m.power_count = kObsMonsterPowerCap + 2;
    for (int p = 0; p < kObsMonsterPowerCap + 2; ++p) {
        m.powers[p] = PowerSlot{static_cast<uint16_t>(PowerId::WEAK),
                                static_cast<int16_t>(p + 1)};
    }

    ObsBuffer obs{};
    encode_observation(s, obs);

    EXPECT_EQ(obs.monsters[0].power_count, kObsMonsterPowerCap + 2);
    for (int p = 0; p < kObsMonsterPowerCap; ++p) {
        EXPECT_EQ(obs.monsters[0].powers[p].amount, p + 1);
    }
}

// --- Zero allocation --------------------------------------------------------

TEST(Observation, EncodeDoesNotAllocate) {
    const CombatState s = make_sample_state();
    ObsBuffer obs{};
    // Warm anything lazy touched by the arguments BEFORE the measured window.
    encode_observation(s, obs);

    const std::size_t before = g_alloc_count;
    encode_observation(s, obs);  // <- the only statement in the measured scope
    const std::size_t after = g_alloc_count;

    EXPECT_EQ(after, before) << "encode_observation allocated on the heap";
}

}  // namespace
}  // namespace sts::engine
