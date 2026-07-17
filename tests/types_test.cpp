// Core id enums and the small POD instance/action types from
// include/sts/engine/types.hpp. No golden-vector dependency -- pure C++ type
// scaffolding.

#include "sts/engine/types.hpp"

#include <type_traits>

#include <gtest/gtest.h>

// Regression guard duplicating the header's load-bearing asserts (design doc
// §4.1/§4.2).
static_assert(std::is_trivially_copyable_v<sts::engine::CardInstance>);
static_assert(sizeof(sts::engine::CardInstance) == 8);
static_assert(std::is_trivially_copyable_v<sts::engine::PowerSlot>);
static_assert(sizeof(sts::engine::PowerSlot) == 4);
static_assert(std::is_trivially_copyable_v<sts::engine::Action>);
static_assert(sizeof(sts::engine::Action) == 4);

namespace {

using namespace sts::engine;

// --- Smoke: construct one of each type -------------------------------------

TEST(TypesSmoke, ConstructsEveryIdEnum) {
    constexpr CardId card = CardId::STRIKE;
    constexpr PowerId power = PowerId::STRENGTH;
    constexpr MonsterId monster = MonsterId::JAW_WORM;
    constexpr RelicId relic = RelicId::NONE;

    EXPECT_EQ(static_cast<uint16_t>(card), 1);
    EXPECT_EQ(static_cast<uint16_t>(power), 1);
    EXPECT_EQ(static_cast<uint16_t>(monster), 1);
    EXPECT_EQ(static_cast<uint16_t>(relic), 0);

    // NONE == 0 sentinel convention, checked for every enum.
    EXPECT_EQ(static_cast<uint16_t>(CardId::NONE), 0);
    EXPECT_EQ(static_cast<uint16_t>(PowerId::NONE), 0);
    EXPECT_EQ(static_cast<uint16_t>(MonsterId::NONE), 0);
    EXPECT_EQ(static_cast<uint16_t>(RelicId::NONE), 0);
}

TEST(TypesSmoke, AllFiveSkeletonCardsAreDistinct) {
    constexpr CardId cards[] = {CardId::STRIKE, CardId::DEFEND, CardId::BASH,
                                CardId::SHRUG_IT_OFF, CardId::POMMEL_STRIKE};
    for (size_t i = 0; i < std::size(cards); ++i) {
        for (size_t j = 0; j < std::size(cards); ++j) {
            if (i != j) {
                EXPECT_NE(cards[i], cards[j]) << i << " vs " << j;
            }
        }
    }
}

TEST(TypesSmoke, ConstructsCardInstance) {
    CardInstance ci{};
    ci.card_id = static_cast<uint16_t>(CardId::BASH);
    ci.upgrade = 1;
    ci.cost_now = 2;
    ci.flags = 0;
    ci.misc = 0;

    EXPECT_EQ(ci.card_id, static_cast<uint16_t>(CardId::BASH));
    EXPECT_EQ(ci.upgrade, 1);
    EXPECT_EQ(ci.cost_now, 2);

    // A value-initialized instance is the empty-slot sentinel.
    constexpr CardInstance empty{};
    EXPECT_EQ(empty.card_id, static_cast<uint16_t>(CardId::NONE));
}

TEST(TypesSmoke, ConstructsPowerSlot) {
    PowerSlot ps{};
    ps.power_id = static_cast<uint16_t>(PowerId::VULNERABLE);
    ps.amount = 2;

    EXPECT_EQ(ps.power_id, static_cast<uint16_t>(PowerId::VULNERABLE));
    EXPECT_EQ(ps.amount, 2);

    constexpr PowerSlot empty{};
    EXPECT_EQ(empty.power_id, static_cast<uint16_t>(PowerId::NONE));
    EXPECT_EQ(empty.amount, 0);
}

// --- Action bit-packing round-trip -----------------------------------------

TEST(TypesSmoke, ActionRoundTripsPlayCard) {
    // PLAY_CARD(hand_idx=3, target=0) -- fixed-target card, arg2 unused.
    const Action a = make_action(ActionVerb::PLAY_CARD, 3, 0, 0);
    EXPECT_EQ(action_verb(a), ActionVerb::PLAY_CARD);
    EXPECT_EQ(action_arg0(a), 3);
    EXPECT_EQ(action_arg1(a), 0);
    EXPECT_EQ(action_arg2(a), 0);
}

TEST(TypesSmoke, ActionRoundTripsEndTurn) {
    const Action a = make_action(ActionVerb::END_TURN);
    EXPECT_EQ(action_verb(a), ActionVerb::END_TURN);
    EXPECT_EQ(action_arg0(a), 0);
    EXPECT_EQ(action_arg1(a), 0);
    EXPECT_EQ(action_arg2(a), 0);
    EXPECT_EQ(a.bits, static_cast<uint32_t>(ActionVerb::END_TURN));
}

TEST(TypesSmoke, ActionRoundTripsAllArgBytesIndependently) {
    // Distinct, non-zero, non-overlapping-looking values in every byte lane
    // to catch any shift/mask mistake in make_action/action_arg*.
    const Action a = make_action(ActionVerb::USE_POTION, 0x11, 0x22, 0x33);
    EXPECT_EQ(action_verb(a), ActionVerb::USE_POTION);
    EXPECT_EQ(action_arg0(a), 0x11);
    EXPECT_EQ(action_arg1(a), 0x22);
    EXPECT_EQ(action_arg2(a), 0x33);
    EXPECT_EQ(a.bits, 0x33221102u);

    const Action b = make_action(ActionVerb::CHOOSE, 7);
    EXPECT_EQ(action_verb(b), ActionVerb::CHOOSE);
    EXPECT_EQ(action_arg0(b), 7);
    EXPECT_EQ(action_arg1(b), 0);
    EXPECT_EQ(action_arg2(b), 0);
}

}  // namespace
