// B4.10 -- the ?-room roll, event selection, pool bookkeeping and the dialog
// framework (event_framework.hpp).
//
// Coverage:
//   * the 100-slot roll table's EXACT fill semantics, including the asymmetric
//     min(99,.)/min(100,.) clamp pair (a later fill overwrites slot 99 once
//     the accumulated sizes reach 100).
//   * event_room_roll: exactly one committed eventRng draw; the trap-19 pity
//     float sequence across 20 ?-rooms against a bit-for-bit independent
//     hand-derivation; Tiny Chest's after-the-draw counter, ==4 equality and
//     pity perturbation; Juzu's MONSTER->EVENT conversion in Java order; the
//     leaving-a-shop column zeroing.
//   * membership init (incl. the NoteForYourself ascension gate), the
//     isCursed Ascender's-Bane exclusion, and the filtered draw lists' gates
//     and canonical order.
//   * generate_event: rs.event_rng byte-identical (selection on a discarded
//     throwaway stream), selection == hand-derivation across seeds, removal
//     bookkeeping (shrine picks clear their bit; both-list removal attempted
//     exactly as getShrine does), the empty-filtered-eventList fallback into
//     getShrine, and the all-empty guard.
//   * the dialog phase plumbing through the synthetic proof body: menu
//     rebuild, a conditional (gold-gated) option, multi-screen flow, illegal
//     CHOOSE as a non-corrupting no-op, and FINISHED -> MAP_CHOICE.
//   * dialog dispatch agrees row-for-row with the registry's `implemented`
//     column, so an event with no linked body parks after bookkeeping and the
//     six act-gated one-time specials stay unreachable.

#include "sts/engine/event_framework.hpp"

#include <bit>
#include <cstdint>
#include <initializer_list>

#include <gtest/gtest.h>

#include "sts/engine/cards.hpp"
#include "sts/engine/relics.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"  // the master-deck bottle bits
#include "sts/engine/run_state.hpp"
#include "sts/registry/event_table.hpp"

namespace sts::engine {
namespace {

constexpr int64_t kSeed = 424242;

bool streams_equal(const RngStream& a, const RngStream& b) noexcept {
    return a.s0 == b.s0 && a.s1 == b.s1 && a.counter == b.counter;
}

// Bit-exact float equality (trap 19: the pity floats must reproduce the
// game's float arithmetic bit for bit, not approximately).
::testing::AssertionResult float_bits_equal(float a, float b) {
    if (std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b)) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure()
           << a << " (0x" << std::hex << std::bit_cast<uint32_t>(a)
           << ") != " << std::dec << b << " (0x" << std::hex
           << std::bit_cast<uint32_t>(b) << ")";
}

// A minimal fresh-run RunState for roll/selection tests: act 1, floor 1, the
// resetProbabilities pity values, full Act-1 pools, event_rng at counter 0.
RunState fresh_run_state(int64_t seed, uint8_t ascension) {
    RunState rs{};
    rs.run_seed = seed;
    rs.act = 1;
    rs.floor = 1;
    rs.ascension = ascension;
    rs.gold = 99;
    rs.hp = 68;
    rs.max_hp = 75;
    rs.event_rng = from_seed(seed);
    rs.event_pity_monster = 0.1f;
    rs.event_pity_shop = 0.03f;
    rs.event_pity_treasure = 0.02f;
    init_event_pools(rs);
    return rs;
}

void give_relic(RunState& rs, RelicId id, int16_t counter = 0) {
    rs.relics[rs.relic_count].relic_id = static_cast<uint16_t>(id);
    rs.relics[rs.relic_count].counter = counter;
    ++rs.relic_count;
}

void add_card(RunState& rs, CardId id) {
    CardInstance& c = rs.master_deck[rs.master_deck_count];
    c = CardInstance{};
    c.card_id = static_cast<uint16_t>(id);
    ++rs.master_deck_count;
}

int16_t relic_counter(const RunState& rs, RelicId id) {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id == static_cast<uint16_t>(id)) {
            return rs.relics[i].counter;
        }
    }
    ADD_FAILURE() << "relic not held";
    return -1;
}

// =============================================================================
// The roll table
// =============================================================================

TEST(EventRollTable, BaselineFreshPityLayout) {
    EventRoomResult t[100];
    build_event_roll_table(10, 3, 2, t);
    for (int i = 0; i < 100; ++i) {
        EventRoomResult want = EventRoomResult::EVENT;
        if (i < 10) want = EventRoomResult::MONSTER;
        else if (i < 13) want = EventRoomResult::SHOP;
        else if (i < 15) want = EventRoomResult::TREASURE;
        EXPECT_EQ(t[i], want) << "slot " << i;
    }
}

TEST(EventRollTable, AllZeroSizesLeaveEverySlotEvent) {
    EventRoomResult t[100];
    build_event_roll_table(0, 0, 0, t);
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(t[i], EventRoomResult::EVENT) << "slot " << i;
    }
}

// The asymmetric clamp pair: from = min(99, fillIndex) but to = min(100,
// fillIndex + size), so once fillIndex >= 100 a later fill -- EVEN ONE WITH
// SIZE 0 -- rewrites slot 99. Coding the clamps symmetrically (99/99 or
// 100/100) fails these pins.
TEST(EventRollTable, AsymmetricClampRewritesSlot99) {
    EventRoomResult t[100];
    // Oversized monster column: slots 0-98 MONSTER, but the shop fill
    // ([99,100), size 0) and then the treasure fill rewrite slot 99; the
    // LAST fill wins.
    build_event_roll_table(120, 0, 0, t);
    for (int i = 0; i < 99; ++i) {
        EXPECT_EQ(t[i], EventRoomResult::MONSTER) << "slot " << i;
    }
    EXPECT_EQ(t[99], EventRoomResult::TREASURE);

    // Exactly-100 accumulation: monster 0-49, shop 50-98, then the size-0
    // treasure fill runs [99,100) and takes slot 99 from SHOP.
    build_event_roll_table(50, 50, 0, t);
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(t[i], EventRoomResult::MONSTER) << "slot " << i;
    }
    for (int i = 50; i < 99; ++i) {
        EXPECT_EQ(t[i], EventRoomResult::SHOP) << "slot " << i;
    }
    EXPECT_EQ(t[99], EventRoomResult::TREASURE);
}

// =============================================================================
// event_room_roll
// =============================================================================

TEST(EventRoomRoll, DrawsExactlyOneCommittedEventRngDraw) {
    RunState rs = fresh_run_state(kSeed, 20);
    const RngStream before = rs.event_rng;
    (void)event_room_roll(rs, /*leaving_shop=*/false);
    RngStream expect = before;
    (void)random(expect);  // one nextFloat
    EXPECT_TRUE(streams_equal(rs.event_rng, expect));
    EXPECT_EQ(rs.event_rng.counter, before.counter + 1);
}

TEST(OnEnterRoomRelics, SsserpentHeadAndMawBankFanOutThroughTheGoldDoor) {
    // Relic.onEnterRoom iterates every slot, so duplicate imported copies each
    // fire. gain_gold is the shared door: Ectoplasm suppresses the whole gain.
    RunState gains = fresh_run_state(kSeed, 20);
    give_relic(gains, RelicId::SSSERPENT_HEAD);
    give_relic(gains, RelicId::MAW_BANK);
    give_relic(gains, RelicId::SSSERPENT_HEAD);
    dispatch_on_enter_room_relics(gains, RoomType::Event);
    EXPECT_EQ(gains.gold, 211);  // 99 + 50 + 12 + 50

    RunState blocked = fresh_run_state(kSeed, 20);
    give_relic(blocked, RelicId::SSSERPENT_HEAD);
    give_relic(blocked, RelicId::MAW_BANK);
    give_relic(blocked, RelicId::ECTOPLASM);
    dispatch_on_enter_room_relics(blocked, RoomType::Event);
    EXPECT_EQ(blocked.gold, 99);

    RunState used = fresh_run_state(kSeed, 20);
    give_relic(used, RelicId::MAW_BANK, -2);
    dispatch_on_enter_room_relics(used, RoomType::Event);
    EXPECT_EQ(used.gold, 99);
}

TEST(OnEnterRoomRelics, MawBankGainPrecedesEventEligibilityAndSelection) {
    RunState rs = fresh_run_state(kSeed, 20);
    rs.gold = 23;
    rs.event_membership =
        static_cast<uint16_t>(1u << (static_cast<uint16_t>(
                                        EventId::THE_CLERIC) -
                                    kEventListFirstId));
    rs.shrine_membership = 0;
    rs.special_membership = 0;
    give_relic(rs, RelicId::MAW_BANK);

    uint16_t before[kEventListCount]{};
    EXPECT_EQ(build_event_pool(rs, before, kEventListCount), 0);
    dispatch_on_enter_room_relics(rs, RoomType::Event);
    EXPECT_EQ(rs.gold, 35);
    EXPECT_EQ(generate_event(rs),
              static_cast<uint16_t>(EventId::THE_CLERIC));
}

// MawBank.onEnterRoom (MawBank.java:31-36) carries NO room-type condition, and
// AbstractDungeon.nextRoomTransition's fan-out (AbstractDungeon.java:1755-1757)
// is likewise unconditional on the room kind -- including the boss node, which
// DungeonMap.java:77-87 reaches by assigning `nextRoom` a MonsterRoomBoss and
// calling nextRoomTransitionStart(). So every room kind pays.
TEST(OnEnterRoomRelics, MawBankPaysOnEveryRoomKindWhileUnused) {
    for (const RoomType room :
         {RoomType::Monster, RoomType::Event, RoomType::Elite, RoomType::Rest,
          RoomType::Shop, RoomType::Treasure, RoomType::Boss}) {
        RunState rs = fresh_run_state(kSeed, 20);
        give_relic(rs, RelicId::MAW_BANK);
        dispatch_on_enter_room_relics(rs, room);
        EXPECT_EQ(rs.gold, 111) << "room " << static_cast<int>(room);
        EXPECT_EQ(relic_counter(rs, RelicId::MAW_BANK), 0);
    }
}

// The Java guard is `nextRoom != null` (AbstractDungeon.java:1754); RoomType::
// None IS that null room, so nothing fires. Unreachable through the map (every
// placed node carries a kind), pinned so a future stall path cannot pay 12 gold.
TEST(OnEnterRoomRelics, NoRoomFiresNothing) {
    RunState rs = fresh_run_state(kSeed, 20);
    give_relic(rs, RelicId::MAW_BANK);
    give_relic(rs, RelicId::SSSERPENT_HEAD);
    dispatch_on_enter_room_relics(rs, RoomType::None);
    EXPECT_EQ(rs.gold, 99);
}

// SsserpentHead.onEnterRoom (SsserpentHead.java:29-35) IS gated -- `room
// instanceof EventRoom`. The fan-out sees the PRE-roll room, so the gate holds
// for every ? entry and for no static node.
TEST(OnEnterRoomRelics, SsserpentHeadIsEventOnlyWhileMawBankIsNot) {
    for (const RoomType room :
         {RoomType::Monster, RoomType::Elite, RoomType::Rest, RoomType::Shop,
          RoomType::Treasure, RoomType::Boss}) {
        RunState rs = fresh_run_state(kSeed, 20);
        give_relic(rs, RelicId::SSSERPENT_HEAD);
        give_relic(rs, RelicId::MAW_BANK);
        dispatch_on_enter_room_relics(rs, room);
        EXPECT_EQ(rs.gold, 111) << "room " << static_cast<int>(room);
    }
}

// EternalFeather.onEnterRoom (EternalFeather.java:29-35):
//     if (room instanceof RestRoom) { flash();
//         int amountToGain = player.masterDeck.size() / 5 * 3;
//         player.heal(amountToGain); }
// INTEGER DIVISION FIRST, then x3 -- a 14-card deck heals 6, not 8.
TEST(OnEnterRoomRelics, EternalFeatherHealsFiveCardStepsOnRestEntry) {
    struct Case {
        int deck;
        int heal;
    };
    const Case cases[] = {{0, 0},  {4, 0},  {5, 3},   {9, 3},
                          {10, 6}, {14, 6}, {15, 9}};
    for (const Case& tc : cases) {
        RunState rs = fresh_run_state(kSeed, 20);
        rs.hp = 20;
        rs.max_hp = 75;
        for (int i = 0; i < tc.deck; ++i) add_card(rs, CardId::STRIKE);
        give_relic(rs, RelicId::ETERNAL_FEATHER);
        dispatch_on_enter_room_relics(rs, RoomType::Rest);
        EXPECT_EQ(rs.hp, 20 + tc.heal) << "deck " << tc.deck;
    }
}

TEST(OnEnterRoomRelics, EternalFeatherFiresOnNoOtherRoomKind) {
    for (const RoomType room :
         {RoomType::Monster, RoomType::Event, RoomType::Elite, RoomType::Shop,
          RoomType::Treasure, RoomType::Boss, RoomType::None}) {
        RunState rs = fresh_run_state(kSeed, 20);
        rs.hp = 20;
        for (int i = 0; i < 15; ++i) add_card(rs, CardId::STRIKE);
        give_relic(rs, RelicId::ETERNAL_FEATHER);
        dispatch_on_enter_room_relics(rs, room);
        EXPECT_EQ(rs.hp, 20) << "room " << static_cast<int>(room);
    }
}

// AbstractCreature.heal clamps to maxHealth (AbstractCreature.java:399-402).
TEST(OnEnterRoomRelics, EternalFeatherClampsToMaxHp) {
    RunState rs = fresh_run_state(kSeed, 20);
    rs.max_hp = 75;
    rs.hp = 73;
    for (int i = 0; i < 15; ++i) add_card(rs, CardId::STRIKE);  // would heal 9
    give_relic(rs, RelicId::ETERNAL_FEATHER);
    dispatch_on_enter_room_relics(rs, RoomType::Rest);
    EXPECT_EQ(rs.hp, 75);
}

// MAGIC FLOWER DOES NOT SCALE THIS. MagicFlower.onPlayerHeal
// (MagicFlower.java:30-37) returns the amount unchanged unless
// `getCurrRoom().phase == RoomPhase.COMBAT`, and the onEnterRoom fan-out runs at
// AbstractDungeon.java:1755-1757 -- before setCurrMapNode, before any room's
// onPlayerEntry, and therefore never inside a combat. Named as a test the way
// the discharged Meal Ticket row named it, so a future in-combat heal-modifier
// change cannot quietly start scaling a room-entry heal.
TEST(OnEnterRoomRelics, EternalFeatherIsNotScaledByMagicFlower) {
    RunState rs = fresh_run_state(kSeed, 20);
    rs.hp = 20;
    rs.max_hp = 75;
    for (int i = 0; i < 10; ++i) add_card(rs, CardId::STRIKE);
    give_relic(rs, RelicId::MAGIC_FLOWER);
    give_relic(rs, RelicId::ETERNAL_FEATHER);
    dispatch_on_enter_room_relics(rs, RoomType::Rest);
    EXPECT_EQ(rs.hp, 26);  // 6, not MathUtils.round(6 * 1.5f) == 9
}

// One loop, acquisition order (relic_hooks.hpp:11-19): the gold and the heal
// come from the same pass, and a duplicate copy fires per copy.
TEST(OnEnterRoomRelics, RestEntryFansOutMawBankAndEternalFeatherTogether) {
    RunState rs = fresh_run_state(kSeed, 20);
    rs.hp = 20;
    rs.max_hp = 75;
    for (int i = 0; i < 11; ++i) add_card(rs, CardId::STRIKE);
    give_relic(rs, RelicId::MAW_BANK);
    give_relic(rs, RelicId::ETERNAL_FEATHER);
    give_relic(rs, RelicId::ETERNAL_FEATHER);
    dispatch_on_enter_room_relics(rs, RoomType::Rest);
    EXPECT_EQ(rs.gold, 111);
    EXPECT_EQ(rs.hp, 32);  // 11/5*3 == 6, twice
}

// The used-up encoding is counter == -2 (MawBank.setCounter, MawBank.java:
// 46-53), the same one dispatch_relics_on_spend_gold writes.
TEST(OnEnterRoomRelics, MawBankUsedUpPaysOnNoRoomKind) {
    for (const RoomType room :
         {RoomType::Monster, RoomType::Event, RoomType::Elite, RoomType::Rest,
          RoomType::Shop, RoomType::Treasure, RoomType::Boss}) {
        RunState rs = fresh_run_state(kSeed, 20);
        give_relic(rs, RelicId::MAW_BANK, -2);
        dispatch_on_enter_room_relics(rs, room);
        EXPECT_EQ(rs.gold, 99) << "room " << static_cast<int>(room);
    }
}

TEST(EventRoomRoll, ResultMatchesHandDerivedTableLookupAcrossSeeds) {
    for (int64_t seed = 1; seed <= 64; ++seed) {
        RunState rs = fresh_run_state(seed, 20);
        RngStream probe = rs.event_rng;
        const float f = random(probe);
        const int slot = static_cast<int>(f * 100.0f);
        EventRoomResult want = EventRoomResult::EVENT;
        if (slot < 10) want = EventRoomResult::MONSTER;
        else if (slot < 13) want = EventRoomResult::SHOP;
        else if (slot < 15) want = EventRoomResult::TREASURE;
        EXPECT_EQ(event_room_roll(rs, false), want) << "seed " << seed;
    }
}

// Trap 19, the named test: 20 consecutive ?-room rolls against a fully
// independent float-arithmetic hand-derivation. Chances stay float, ramps are
// float adds, sizes are (int)(chance * 100.0f) truncations; every intermediate
// value must match BIT FOR BIT.
TEST(EventRoomRoll, Trap19PityFloatSequenceAcrossTwentyRooms) {
    RunState rs = fresh_run_state(kSeed, 20);
    RngStream probe = rs.event_rng;  // independent draw replay

    float m = 0.1f;
    float sh = 0.03f;
    float tr = 0.02f;
    for (int room = 0; room < 20; ++room) {
        // Independent expectation.
        const float f = random(probe);
        const int slot = static_cast<int>(f * 100.0f);
        const int m_sz = static_cast<int>(m * 100.0f);
        const int s_sz = static_cast<int>(sh * 100.0f);
        const int t_sz = static_cast<int>(tr * 100.0f);
        EventRoomResult want = EventRoomResult::EVENT;
        if (slot < m_sz) {
            want = EventRoomResult::MONSTER;
        } else if (slot < m_sz + s_sz) {
            want = EventRoomResult::SHOP;
        } else if (slot < m_sz + s_sz + t_sz) {
            want = EventRoomResult::TREASURE;
        }
        m = want == EventRoomResult::MONSTER ? 0.1f : m + 0.1f;
        sh = want == EventRoomResult::SHOP ? 0.03f : sh + 0.03f;
        tr = want == EventRoomResult::TREASURE ? 0.02f : tr + 0.02f;

        const EventRoomResult got = event_room_roll(rs, false);
        ASSERT_EQ(got, want) << "room " << room;
        EXPECT_TRUE(float_bits_equal(rs.event_pity_monster, m)) << "room " << room;
        EXPECT_TRUE(float_bits_equal(rs.event_pity_shop, sh)) << "room " << room;
        EXPECT_TRUE(float_bits_equal(rs.event_pity_treasure, tr)) << "room " << room;
    }
    EXPECT_EQ(rs.event_rng.counter, 20);
}

// Find a seed whose FIRST fresh-run roll float lands in [lo, hi).
int64_t seed_with_first_roll_in(float lo, float hi) {
    for (int64_t s = 1; s < 60000; ++s) {
        RngStream probe = from_seed(s);
        const float f = random(probe);
        if (f >= lo && f < hi) {
            return s;
        }
    }
    ADD_FAILURE() << "no seed with first roll in [" << lo << ", " << hi << ")";
    return 1;
}

TEST(EventRoomRoll, TinyChestForcesOnFourthRoomAndPityObservesTheForce) {
    // A seed whose first four rolls would all be EVENT keeps the underlying
    // choice away from TREASURE, so the force is unambiguously the relic's.
    int64_t seed = 0;
    for (int64_t s = 1; s < 60000 && seed == 0; ++s) {
        RngStream probe = from_seed(s);
        bool all_event = true;
        // Pity after k misses: monster 10+10k / shop 3+3k / treasure 2+2k
        // slots; the strictest bound over rooms 1-4 is slot >= 60+15=75... use
        // f >= 0.90 for all four draws, comfortably EVENT throughout.
        for (int k = 0; k < 4; ++k) {
            all_event = all_event && random(probe) >= 0.90f;
        }
        if (all_event) seed = s;
    }
    ASSERT_NE(seed, 0) << "no all-EVENT-x4 seed found";

    RunState rs = fresh_run_state(seed, 20);
    give_relic(rs, RelicId::TINY_CHEST, 0);  // onEquip counter (TinyChest.java:30-32)

    // Rooms 1-3: counter climbs AFTER each draw, no force.
    EXPECT_EQ(event_room_roll(rs, false), EventRoomResult::EVENT);
    EXPECT_EQ(relic_counter(rs, RelicId::TINY_CHEST), 1);
    EXPECT_EQ(event_room_roll(rs, false), EventRoomResult::EVENT);
    EXPECT_EQ(relic_counter(rs, RelicId::TINY_CHEST), 2);
    EXPECT_EQ(event_room_roll(rs, false), EventRoomResult::EVENT);
    EXPECT_EQ(relic_counter(rs, RelicId::TINY_CHEST), 3);

    // Pity going into room 4 (three EVENT misses).
    const float m3 = rs.event_pity_monster;
    const float s3 = rs.event_pity_shop;

    // Room 4: ++counter == 4 -> reset to 0, force TREASURE -- and every pity
    // update observes the FORCED choice (the override lands before them,
    // EventHelper.java:144-146): treasure resets, monster/shop ramp.
    EXPECT_EQ(event_room_roll(rs, false), EventRoomResult::TREASURE);
    EXPECT_EQ(relic_counter(rs, RelicId::TINY_CHEST), 0);
    EXPECT_TRUE(float_bits_equal(rs.event_pity_treasure, 0.02f));
    EXPECT_TRUE(float_bits_equal(rs.event_pity_monster, m3 + 0.1f));
    EXPECT_TRUE(float_bits_equal(rs.event_pity_shop, s3 + 0.03f));
}

TEST(EventRoomRoll, TinyChestTriggerIsEqualityNotAtLeast) {
    // == 4, not >= 4 (EventHelper.java:108): an imported counter already past
    // 4 keeps climbing and never forces.
    const int64_t seed = seed_with_first_roll_in(0.90f, 1.0f);  // EVENT slot
    RunState rs = fresh_run_state(seed, 20);
    give_relic(rs, RelicId::TINY_CHEST, 4);
    EXPECT_EQ(event_room_roll(rs, false), EventRoomResult::EVENT);  // no force
    EXPECT_EQ(relic_counter(rs, RelicId::TINY_CHEST), 5);
}

TEST(EventRoomRoll, DuplicateTinyChestImportUsesFirstInstanceOnly) {
    // EventHelper.roll calls hasRelic then getRelic("Tiny Chest"), and
    // AbstractPlayer.getRelic returns the first matching slot. A malformed or
    // imported RunState can contain duplicates even though normal acquisition
    // does not, so the hook must neither increment nor trigger a later copy.
    const int64_t seed = seed_with_first_roll_in(0.90f, 1.0f);  // EVENT slot

    RunState first_triggers = fresh_run_state(seed, 20);
    give_relic(first_triggers, RelicId::TINY_CHEST, 3);
    give_relic(first_triggers, RelicId::TINY_CHEST, 0);
    EXPECT_EQ(event_room_roll(first_triggers, false),
              EventRoomResult::TREASURE);
    EXPECT_EQ(first_triggers.relics[0].counter, 0);
    EXPECT_EQ(first_triggers.relics[1].counter, 0);

    RunState later_would_trigger = fresh_run_state(seed, 20);
    give_relic(later_would_trigger, RelicId::TINY_CHEST, 0);
    give_relic(later_would_trigger, RelicId::TINY_CHEST, 3);
    EXPECT_EQ(event_room_roll(later_would_trigger, false),
              EventRoomResult::EVENT);
    EXPECT_EQ(later_would_trigger.relics[0].counter, 1);
    EXPECT_EQ(later_would_trigger.relics[1].counter, 3);
}

TEST(EventRoomRoll, JuzuConvertsMonsterToEventAndMonsterPityStillResets) {
    // Widen the monster column to 50 slots and land the roll inside it (past
    // the fresh 10 so the conversion, not the base layout, is what is
    // proved).
    const int64_t seed = seed_with_first_roll_in(0.20f, 0.49f);

    // Without Juzu: a plain MONSTER hit.
    RunState plain = fresh_run_state(seed, 20);
    plain.event_pity_monster = 0.5f;
    EXPECT_EQ(event_room_roll(plain, false), EventRoomResult::MONSTER);
    EXPECT_TRUE(float_bits_equal(plain.event_pity_monster, 0.1f));

    // With Juzu: the conversion at EventHelper.java:158 runs INSIDE the
    // MONSTER branch, before the reset at :160 -- the caller sees EVENT, the
    // monster pity still resets, and shop/treasure (post-Juzu observers) see
    // EVENT and ramp.
    RunState rs = fresh_run_state(seed, 20);
    rs.event_pity_monster = 0.5f;
    give_relic(rs, RelicId::JUZU_BRACELET);
    EXPECT_EQ(event_room_roll(rs, false), EventRoomResult::EVENT);
    EXPECT_TRUE(float_bits_equal(rs.event_pity_monster, 0.1f));
    EXPECT_TRUE(float_bits_equal(rs.event_pity_shop, 0.03f + 0.03f));
    EXPECT_TRUE(float_bits_equal(rs.event_pity_treasure, 0.02f + 0.02f));
}

TEST(EventRoomRoll, LeavingAShopZeroesTheShopColumn) {
    // Widen the shop column to 50 (slots 10-59 with fresh monster 10) and
    // land the roll inside it.
    const int64_t seed = seed_with_first_roll_in(0.20f, 0.59f);

    RunState hit = fresh_run_state(seed, 20);
    hit.event_pity_shop = 0.5f;
    EXPECT_EQ(event_room_roll(hit, /*leaving_shop=*/false), EventRoomResult::SHOP);
    EXPECT_TRUE(float_bits_equal(hit.event_pity_shop, 0.03f));  // hit -> reset

    // Leaving a shop: shopSize = 0 (EventHelper.java:128-130) -- the SLOT
    // becomes EVENT, but the pity FIELD still ramps (the update reads
    // SHOP_CHANCE, not shopSize).
    RunState gated = fresh_run_state(seed, 20);
    gated.event_pity_shop = 0.5f;
    EXPECT_EQ(event_room_roll(gated, /*leaving_shop=*/true), EventRoomResult::EVENT);
    EXPECT_TRUE(float_bits_equal(gated.event_pity_shop, 0.5f + 0.03f));
}

// =============================================================================
// Membership + filtered pools
// =============================================================================

TEST(EventPools, InitSetsFullActOneListsWithNoteForYourselfGate) {
    for (int asc : {0, 1, 14}) {
        RunState rs{};
        rs.ascension = static_cast<uint8_t>(asc);
        init_event_pools(rs);
        EXPECT_EQ(rs.event_membership, 0x07FFu) << "asc " << asc;
        EXPECT_EQ(rs.shrine_membership, 0x3Fu) << "asc " << asc;
        EXPECT_EQ(rs.special_membership, 0x3FFFu) << "asc " << asc;
    }
    for (int asc : {15, 20}) {
        RunState rs{};
        rs.ascension = static_cast<uint8_t>(asc);
        init_event_pools(rs);
        // No placeholder: only the NFY bit is absent; every other bit keeps
        // its canonical NFY-present position.
        EXPECT_EQ(rs.special_membership,
                  0x3FFFu & ~(1u << kNoteForYourselfBit)) << "asc " << asc;
    }
}

TEST(EventPools, IsCursedExcludesTheThreeSpecialCurses) {
    // AbstractPlayer.isCursed skips AscendersBane, CurseOfTheBell, and
    // Necronomicurse (AbstractPlayer.java:744-746). The first two have S1
    // rows; Necronomicurse does not.
    RunState rs{};
    add_card(rs, CardId::ASCENDERS_BANE);
    add_card(rs, CardId::STRIKE);
    EXPECT_FALSE(event_player_is_cursed(rs));
    add_card(rs, CardId::CURSE_OF_THE_BELL);
    EXPECT_FALSE(event_player_is_cursed(rs));
    add_card(rs, CardId::CLUMSY);
    EXPECT_TRUE(event_player_is_cursed(rs));
}

TEST(EventPools, EventPoolAppliesFloorAndGoldGates) {
    RunState rs = fresh_run_state(kSeed, 20);
    uint16_t pool[16];

    // Floor 1, gold 99: Dead Adventurer + Mushrooms out (floorNum <= 6).
    rs.floor = 1;
    int n = build_event_pool(rs, pool, 16);
    ASSERT_EQ(n, 9);
    const uint16_t want_f1[9] = {1, 2, 4, 5, 6, 7, 8, 10, 11};
    for (int i = 0; i < 9; ++i) EXPECT_EQ(pool[i], want_f1[i]) << "i " << i;

    // Floor 6 is still gated (floorNum <= 6, an inclusive skip); floor 7 is in.
    rs.floor = 6;
    EXPECT_EQ(build_event_pool(rs, pool, 16), 9);
    rs.floor = 7;
    n = build_event_pool(rs, pool, 16);
    ASSERT_EQ(n, 11);
    const uint16_t want_f7[11] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    for (int i = 0; i < 11; ++i) EXPECT_EQ(pool[i], want_f7[i]) << "i " << i;

    // The Cleric needs gold >= 35 (strict < 35 skip).
    rs.gold = 34;
    EXPECT_EQ(build_event_pool(rs, pool, 16), 10);
    rs.gold = 35;
    EXPECT_EQ(build_event_pool(rs, pool, 16), 11);
}

TEST(EventPools, ShrinePoolGatesAndCanonicalOrderInActOne) {
    // A20 (no NFY), gold 49, uncursed: 6 shrines + the 5 unconditional Act-1
    // specials, in canonical list order -- shrines first (tmp.addAll), then
    // specials in insertion order.
    RunState rs = fresh_run_state(kSeed, 20);
    rs.gold = 49;
    uint16_t pool[20];
    int n = build_shrine_pool(rs, pool, 20);
    ASSERT_EQ(n, 11);
    const uint16_t want[11] = {12, 13, 14, 15, 16, 17,   // shrines
                               18, 19, 22, 25, 30};      // specials, gated
    for (int i = 0; i < 11; ++i) EXPECT_EQ(pool[i], want[i]) << "i " << i;

    // gold >= 50 admits The Woman in Blue at the list tail.
    rs.gold = 50;
    n = build_shrine_pool(rs, pool, 20);
    ASSERT_EQ(n, 12);
    EXPECT_EQ(pool[11], 31);

    // A real curse admits the Fountain at its canonical position (after
    // FaceTrader, before Lab).
    add_card(rs, CardId::CLUMSY);
    n = build_shrine_pool(rs, pool, 20);
    ASSERT_EQ(n, 13);
    const uint16_t want_cursed[13] = {12, 13, 14, 15, 16, 17,
                                      18, 19, 22, 23, 25, 30, 31};
    for (int i = 0; i < 13; ++i) EXPECT_EQ(pool[i], want_cursed[i]) << "i " << i;

    // A0 additionally holds NoteForYourself between N'loth's (absent) slot
    // and SecretPortal's -- i.e. after Lab, before WeMeetAgain.
    RunState a0 = fresh_run_state(kSeed, 0);
    a0.gold = 49;
    n = build_shrine_pool(a0, pool, 20);
    ASSERT_EQ(n, 12);
    const uint16_t want_a0[12] = {12, 13, 14, 15, 16, 17,
                                  18, 19, 22, 25, 27, 30};
    for (int i = 0; i < 12; ++i) EXPECT_EQ(pool[i], want_a0[i]) << "i " << i;
}

// =============================================================================
// generate_event
// =============================================================================

TEST(GenerateEvent, LeavesEventRngByteIdentical) {
    for (int64_t seed = 1; seed <= 32; ++seed) {
        RunState rs = fresh_run_state(seed, 20);
        const RngStream before = rs.event_rng;
        const uint16_t id = generate_event(rs);
        EXPECT_NE(id, 0) << "seed " << seed;
        EXPECT_TRUE(streams_equal(rs.event_rng, before)) << "seed " << seed;
    }
}

TEST(GenerateEvent, SelectionMatchesHandDerivationAcrossSeeds) {
    // Fresh A20 state at floor 1 with 99 gold: the filtered pools are the
    // hardcoded lists below (event: 9 entries, no Dead Adventurer/Mushrooms;
    // shrine: 12 entries incl. The Woman in Blue, no NFY). Hand-derive the
    // throwaway-stream draws: split = random(1.0f) vs 0.25, then ONE inclusive
    // index draw over the filtered list.
    const uint16_t event_pool[9] = {1, 2, 4, 5, 6, 7, 8, 10, 11};
    const uint16_t shrine_pool[12] = {12, 13, 14, 15, 16, 17,
                                      18, 19, 22, 25, 30, 31};
    int shrine_picks = 0;
    int event_picks = 0;
    for (int64_t seed = 1; seed <= 40; ++seed) {
        RunState rs = fresh_run_state(seed, 20);
        RngStream local = rs.event_rng;  // the game's duplicate #2
        uint16_t want = 0;
        if (random(local, 1.0f) < 0.25f) {
            want = shrine_pool[random(local, 11)];
            ++shrine_picks;
        } else {
            want = event_pool[random(local, 8)];
            ++event_picks;
        }
        const uint16_t got = generate_event(rs);
        EXPECT_EQ(got, want) << "seed " << seed;
        // Removal + fired bookkeeping committed for exactly the selected id.
        EXPECT_EQ(rs.event_flags, 1u << (want - 1u)) << "seed " << seed;
    }
    // 40 seeds must exercise both branches or the derivation proved nothing.
    EXPECT_GT(shrine_picks, 0);
    EXPECT_GT(event_picks, 0);
}

// Acceptance's live §2.5 pool-removal check: three independent real campaign
// seeds whose first resolved EVENT removed the named key from eventList. The
// compact vectors below are the post-room-roll eventRng triples and player
// gates captured in _oracle_data/campaigns/b14_accept2 (raw artifacts remain
// uncommitted by policy):
//   STS00004 floor 3 -> Scrap Ooze
//   STS00007 floor 2 -> Living Wall
//   STS00008 floor 2 -> Big Fish
// Selection is on the post-roll duplicate, so the oracle triple is also the
// exact stream state generate_event must leave byte-identical.
TEST(GenerateEvent, PoolRemovalMatchesLiveOracleAcrossThreeSeeds) {
    struct Case {
        int64_t seed;
        uint8_t floor;
        int32_t gold;
        int16_t hp;
        int16_t max_hp;
        int64_t s0;
        int64_t s1;
        EventId selected;
    };
    const Case cases[] = {
        {1790050543754LL, 3, 124, 75, 75, 8661588806171028713LL,
         5191807700964790461LL, EventId::SCRAP_OOZE},
        {1790050543757LL, 2, 110, 33, 75, -1202442134681472275LL,
         -6939658151312417514LL, EventId::LIVING_WALL},
        {1790050543758LL, 2, 99, 60, 68, 6818110329401097880LL,
         6213027136388943737LL, EventId::BIG_FISH},
    };

    for (const Case& tc : cases) {
        SCOPED_TRACE(tc.seed);
        RunState rs = fresh_run_state(tc.seed, 20);
        rs.floor = tc.floor;
        rs.gold = tc.gold;
        rs.hp = tc.hp;
        rs.max_hp = tc.max_hp;
        rs.event_rng.counter = 1;
        rs.event_rng.s0 = static_cast<uint64_t>(tc.s0);
        rs.event_rng.s1 = static_cast<uint64_t>(tc.s1);
        const RngStream before = rs.event_rng;
        const uint16_t selected = static_cast<uint16_t>(tc.selected);

        EXPECT_EQ(generate_event(rs), selected);
        EXPECT_TRUE(streams_equal(rs.event_rng, before));
        EXPECT_EQ(rs.event_membership,
                  0x07FFu & ~(1u << (selected - kEventListFirstId)));
        EXPECT_EQ(rs.shrine_membership, 0x3Fu);
        EXPECT_EQ(rs.special_membership,
                  0x3FFFu & ~(1u << kNoteForYourselfBit));
        EXPECT_EQ(rs.event_flags, 1u << (selected - 1u));
    }
}

TEST(GenerateEvent, ShrinePickClearsItsShrineBitOnly) {
    // Only Transmorgrifier (id 14, shrine bit 2) is left anywhere.
    RunState rs = fresh_run_state(kSeed, 20);
    rs.event_membership = 0;
    rs.shrine_membership = 1u << 2;
    rs.special_membership = 0;
    EXPECT_EQ(generate_event(rs), 14);
    EXPECT_EQ(rs.shrine_membership, 0);
    EXPECT_EQ(rs.special_membership, 0);
    EXPECT_EQ(rs.event_flags, 1u << 13);
}

TEST(GenerateEvent, SpecialPickClearsItsSpecialBitOnly) {
    // Only FaceTrader (id 22, special bit 4) remains; both branches of the
    // split reach it (shrine branch directly, event branch via the
    // empty-filtered-eventList fallback into getShrine).
    RunState rs = fresh_run_state(kSeed, 20);
    rs.event_membership = 0;
    rs.shrine_membership = 0;
    rs.special_membership = 1u << 4;
    EXPECT_EQ(generate_event(rs), 22);
    EXPECT_EQ(rs.special_membership, 0);
    EXPECT_EQ(rs.shrine_membership, 0);
    EXPECT_EQ(rs.event_flags, 1u << 21);
}

TEST(GenerateEvent, EmptyFilteredEventListFallsBackToShrines) {
    // eventList raw-nonempty but fully filtered (only Dead Adventurer, at
    // floor 1): getEvent's tmp is empty -> getShrine (AbstractDungeon.java:
    // 1983-1985). Pick a seed on the EVENT side of the split so the fallback
    // path itself is what selects the shrine.
    const int64_t seed = seed_with_first_roll_in(0.25f, 1.0f);
    RunState rs = fresh_run_state(seed, 20);
    rs.floor = 1;
    rs.event_membership = 1u << 2;  // Dead Adventurer only -- filtered out
    rs.shrine_membership = 1u << 0; // Match and Keep only
    rs.special_membership = 0;
    EXPECT_EQ(generate_event(rs), 12);
    EXPECT_EQ(rs.event_membership, 1u << 2);  // never removed, only filtered
    EXPECT_EQ(rs.shrine_membership, 0);
}

TEST(GenerateEvent, AllPoolsEmptyReturnsZeroWithoutCommit) {
    RunState rs = fresh_run_state(kSeed, 20);
    rs.event_membership = 0;
    rs.shrine_membership = 0;
    rs.special_membership = 0;
    const RngStream before = rs.event_rng;
    EXPECT_EQ(generate_event(rs), 0);
    EXPECT_EQ(rs.event_flags, 0u);
    EXPECT_TRUE(streams_equal(rs.event_rng, before));
}

// =============================================================================
// Dialog dispatch + the EVENT_DIALOG phase (through the synthetic proof body)
// =============================================================================

TEST(EventDialog, DispatchMatchesImplementedRegistryRows) {
    // The dispatch table is generated from the registry's `implemented` column,
    // so the two must agree row for row -- including the deliberate holes left
    // by the six one-time specials whose getShrine gates exclude them from
    // Act 1 (AbstractDungeon.java:1894-1933).
    for (uint16_t id = 1; id <= 31; ++id) {
        const sts::registry::EventDef* def =
            sts::registry::event_def(static_cast<EventId>(id));
        ASSERT_NE(def, nullptr) << "EventId " << id;
        if (def->implemented) {
            EXPECT_NE(event_dialog_impl(id), nullptr) << "EventId " << id;
        } else {
            EXPECT_EQ(event_dialog_impl(id), nullptr) << "EventId " << id;
        }
    }
    // The act-gated one-timers now HAVE bodies (S2.32) -- they stay
    // Act-1-unreachable through their getShrine act gates, not through a
    // dispatch hole. The two whose bodies remain unlanded stay pinned null:
    // Lab's listing question and SecretPortal's playtime pin are S2.33's.
    for (const EventId id : {EventId::DESIGNER, EventId::DUPLICATOR,
                             EventId::KNOWING_SKULL, EventId::NLOTH,
                             EventId::THE_JOUST}) {
        EXPECT_NE(event_dialog_impl(static_cast<uint16_t>(id)), nullptr);
    }
    EXPECT_EQ(event_dialog_impl(
                  static_cast<uint16_t>(EventId::SECRET_PORTAL)), nullptr);
    EXPECT_EQ(event_dialog_impl(0), nullptr);
    EXPECT_NE(event_dialog_impl(kSyntheticEventId), nullptr);
}

RunController synthetic_dialog_controller(int32_t gold) {
    RunController rc = run_begin(kSeed, 0);
    rc.run.gold = gold;
    rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
    rc.room_type = static_cast<uint8_t>(RoomType::Event);
    rc.event = EventDialogState{};
    rc.event.event_id = kSyntheticEventId;
    const EventDialogImpl* impl = event_dialog_impl(kSyntheticEventId);
    impl->on_enter(rc, rc.event);
    return rc;
}

void dialog_step(RunController& rc, uint8_t option) {
    Action a = make_action(ActionVerb::CHOOSE, option);
    StepResult res{};
    advance(std::span<RunController>(&rc, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&res, 1));
}

TEST(EventDialog, SyntheticFullFlowWithConditionalOption) {
    RunController rc = synthetic_dialog_controller(/*gold=*/99);

    // Screen 0: four options, the gold-gated one enabled at 99.
    RunActionMask m{};
    legal_actions(rc, m);
    EXPECT_TRUE(m.can_choose_event_option[0]);
    EXPECT_TRUE(m.can_choose_event_option[1]);
    EXPECT_TRUE(m.can_choose_event_option[2]);
    EXPECT_TRUE(m.can_choose_event_option[3]);
    for (int i = 4; i < kEventOptionCap; ++i) {
        EXPECT_FALSE(m.can_choose_event_option[i]) << "option " << i;
    }

    // Pay 50 gold for +5 max HP -> screen 1 (CONTINUE keeps the phase).
    const int16_t hp = rc.run.hp;
    const int16_t max_hp = rc.run.max_hp;
    dialog_step(rc, 1);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::EVENT_DIALOG));
    EXPECT_EQ(rc.run.gold, 49);
    EXPECT_EQ(rc.run.max_hp, max_hp + 5);
    EXPECT_EQ(rc.run.hp, hp + 5);
    EXPECT_EQ(rc.event.screen, 1);
    EXPECT_EQ(rc.event.scratch0, 1);  // event-defined state persists

    // Screen 1: exactly one option; taking it FINISHES -> MAP_CHOICE with the
    // dialog state cleared.
    legal_actions(rc, m);
    EXPECT_TRUE(m.can_choose_event_option[0]);
    EXPECT_FALSE(m.can_choose_event_option[1]);
    dialog_step(rc, 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(rc.event.event_id, 0);
    EXPECT_EQ(rc.event.screen, 0);
}

TEST(EventDialog, SyntheticGoldGainUsesTheEctoplasmAwareDoor) {
    RunController rc = synthetic_dialog_controller(/*gold=*/99);
    give_relic(rc.run, RelicId::ECTOPLASM);
    dialog_step(rc, 0);
    EXPECT_EQ(rc.run.gold, 99);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::EVENT_DIALOG));
    EXPECT_EQ(rc.event.screen, 1);
}

TEST(EventDialog, BodyOwnedTransitionIsNotOverwrittenByStepOne) {
    RunController rc = synthetic_dialog_controller(/*gold=*/99);
    dialog_step(rc, 3);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::ROOM_UNIMPLEMENTED));
    EXPECT_EQ(rc.event.event_id, 0);
}

TEST(EventDialog, DisabledAndOutOfRangeChoicesAreNonCorruptingNoOps) {
    RunController rc = synthetic_dialog_controller(/*gold=*/30);

    RunActionMask m{};
    legal_actions(rc, m);
    EXPECT_TRUE(m.can_choose_event_option[0]);
    EXPECT_FALSE(m.can_choose_event_option[1]);  // gold < 50

    const RunController before = rc;
    dialog_step(rc, 1);  // disabled option
    EXPECT_EQ(rc.phase, before.phase);
    EXPECT_EQ(rc.run.gold, 30);
    EXPECT_EQ(rc.event.screen, 0);
    dialog_step(rc, 7);  // beyond the menu count
    EXPECT_EQ(rc.run.gold, 30);
    EXPECT_EQ(rc.event.screen, 0);

    // Leaving still works.
    dialog_step(rc, 2);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
}

// =============================================================================
// Bottled-card exclusion on the event grids (getGroupWithoutBottledCards)
// =============================================================================
//
// Every S1 event PURGE/TRANSFORMABLE grid opens
// getGroupWithoutBottledCards(getPurgeableCards()) (Cleric.java:76-78,
// LivingWall.java:96-103, Bonfire.java:101-102, PurificationShrine.java:62,
// Transmogrifier.java:65, GremlinWheelGame.java:286-287,
// NoteForYourself.java:65, GoldenWing.java:110). The UPGRADE grid does NOT
// exclude: LivingWall's Grow opens getUpgradableCards() (:109). And because
// events::has_purgeable_card is grid_has_card(PURGE) over the same legality,
// every event's purge/transform OPTION gate picks up the exclusion too.

TEST(EventGridBottle, PurgeAndTransformGridsExcludeBottledCardsUpgradeKeepsThem) {
    RunState rs{};
    rs.master_deck_count = 2;
    rs.master_deck[0].card_id = static_cast<uint16_t>(CardId::CLEAVE);
    rs.master_deck[0].flags = kMasterCardInBottleFlame;
    rs.master_deck[1].card_id = static_cast<uint16_t>(CardId::ARMAMENTS);

    EventDialogState es{};
    open_event_grid(es, EventGridKind::PURGE);
    EXPECT_FALSE(event_grid_card_legal(rs, es, 0))
        << "a bottled card is not on any S1 event purge grid";
    EXPECT_TRUE(event_grid_card_legal(rs, es, 1));

    open_event_grid(es, EventGridKind::TRANSFORMABLE);
    EXPECT_FALSE(event_grid_card_legal(rs, es, 0));
    EXPECT_TRUE(event_grid_card_legal(rs, es, 1));

    open_event_grid(es, EventGridKind::UPGRADE);
    EXPECT_TRUE(event_grid_card_legal(rs, es, 0))
        << "upgrade grids keep bottled cards (LivingWall.java:109)";
}

TEST(EventGridBottle, AnAllBottledPurgeableDeckClosesTheEventPurgeGates) {
    // events::has_purgeable_card is grid_has_card(PURGE): when every purgeable
    // card is bottled, the purge/transform option gates read false and no
    // event opens an empty grid.
    RunState rs{};
    rs.master_deck_count = 2;
    rs.master_deck[0].card_id = static_cast<uint16_t>(CardId::CLEAVE);
    rs.master_deck[0].flags = kMasterCardInBottleFlame;
    rs.master_deck[1].card_id = static_cast<uint16_t>(CardId::ASCENDERS_BANE);

    EventDialogState probe{};
    open_event_grid(probe, EventGridKind::PURGE);
    bool any = false;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        any = any || event_grid_card_legal(rs, probe, i);
    }
    EXPECT_FALSE(any);
}


// =============================================================================
// S2.13 -- ?-rooms across acts: the per-act list rebuild, the one-time pool's
// cross-act depletion, the act-gated one-time draw filters, the three Act-2/3
// event gates, and the two-word FIRED bitset.
// =============================================================================

// An Act-2 or Act-3 RunState with that act's pools freshly rebuilt, exactly as
// act_transition leaves them. `floor` is the act's first playable room unless a
// test moves it; gold/hp are deliberately mid-range so each gate's boundary has
// to be set explicitly rather than passing by accident.
RunState fresh_act_state(int act, int64_t seed = kSeed, uint8_t ascension = 20) {
    RunState rs = fresh_run_state(seed, ascension);
    rs.act = static_cast<uint8_t>(act);
    rs.floor = static_cast<uint16_t>((act - 1) * 17 + 1);
    reinit_act_event_pools(rs);
    return rs;
}

// Is `id` in the act's filtered shrine draw list?
bool special_offered(const RunState& rs, uint16_t id) {
    uint16_t pool[kShrineListCount + kSpecialListCount];
    const int n =
        build_shrine_pool(rs, pool, kShrineListCount + kSpecialListCount);
    for (int i = 0; i < n; ++i) {
        if (pool[i] == id) return true;
    }
    return false;
}

// A state with only the one special under test left in the pool, so a gate's
// verdict cannot be masked by a neighbour.
RunState only_special(int act, uint16_t id) {
    RunState rs = fresh_act_state(act);
    rs.shrine_membership = 0;
    rs.special_membership =
        static_cast<uint16_t>(1u << (id - kSpecialListFirstId));
    rs.gold = 0;
    return rs;
}

bool event_offered(const RunState& rs, uint16_t id) {
    uint16_t pool[kEventListMaxCount];
    const int n = build_event_pool(rs, pool, kEventListMaxCount);
    for (int i = 0; i < n; ++i) {
        if (pool[i] == id) return true;
    }
    return false;
}

// --- A. the per-act list rebuild ---------------------------------------------

TEST(S213ActPools, ActTwoEventPoolIsTheThirteenCityRows) {
    // TheCity.initializeEventList (TheCity.java:185-198), hand-carried in add
    // order: Addict, Back to Basics, Beggar, Colosseum, Cursed Tome, Drug
    // Dealer, Forgotten Altar, Ghosts, Masked Bandits, Nest, The Library,
    // The Mausoleum, Vampires == registry ids 32..44.
    RunState rs = fresh_act_state(2);
    rs.gold = 999;      // clears Beggar's gold >= 75
    rs.floor = 17 + 9;  // row 8: clears Colosseum's row gate
    uint16_t pool[kEventListMaxCount];
    const int n = build_event_pool(rs, pool, kEventListMaxCount);
    ASSERT_EQ(n, 13);
    for (int i = 0; i < 13; ++i) {
        EXPECT_EQ(pool[i], 32 + i) << "position " << i;
    }
    EXPECT_EQ(event_list_first_id(2), 32u);
    EXPECT_EQ(event_list_count(2), 13);
}

TEST(S213ActPools, ActThreeEventPoolIsTheSevenBeyondRows) {
    // TheBeyond.initializeEventList (TheBeyond.java:179-186): Falling,
    // MindBloom, The Moai Head, Mysterious Sphere, SensoryStone, Tomb of Lord
    // Red Mask, Winding Halls == ids 45..51.
    RunState rs = fresh_act_state(3);
    rs.hp = 10;  // clears Moai Head's <= 50 % HP arm
    rs.max_hp = 75;
    uint16_t pool[kEventListMaxCount];
    const int n = build_event_pool(rs, pool, kEventListMaxCount);
    ASSERT_EQ(n, 7);
    for (int i = 0; i < 7; ++i) {
        EXPECT_EQ(pool[i], 45 + i) << "position " << i;
    }
    EXPECT_EQ(event_list_first_id(3), 45u);
    EXPECT_EQ(event_list_count(3), 7);
}

TEST(S213ActPools, ActTwoShrineDrawOrderPutsWheelOfChangeSecond) {
    // THE S2.02 finding's engine-side twin. Exordium ends with Wheel of Change
    // (Exordium.java:238-246); TheCity and TheBeyond -- byte-identical to each
    // other -- put it SECOND (TheCity.java:210-218 == TheBeyond.java:198-206).
    // Same six keys, two orders, and the draw index resolves against the order.
    uint16_t pool[kShrineListCount + kSpecialListCount];
    RunState a1 = fresh_act_state(1);
    a1.gold = 0;                // no Woman in Blue
    a1.special_membership = 0;  // shrines only, so the order is unambiguous
    ASSERT_EQ(build_shrine_pool(a1, pool, 20), 6);
    const uint16_t want_a1[6] = {12, 13, 14, 15, 16, 17};
    for (int i = 0; i < 6; ++i) EXPECT_EQ(pool[i], want_a1[i]) << "i " << i;

    for (int act : {2, 3}) {
        SCOPED_TRACE("act " + std::to_string(act));
        RunState rs = fresh_act_state(act);
        rs.gold = 0;
        rs.special_membership = 0;
        ASSERT_EQ(build_shrine_pool(rs, pool, 20), 6);
        const uint16_t want[6] = {12, 17, 13, 14, 15, 16};
        for (int i = 0; i < 6; ++i) EXPECT_EQ(pool[i], want[i]) << "i " << i;
    }
}

TEST(S213ActPools, ShrineBitMeaningIsActIndependent) {
    // The deliberate split: `shrine_membership` bit i is EventId (12+i) in
    // every act -- so the bitset stays byte-comparable across a crossing for
    // the differ and PublicView, neither of which knows the act at compare
    // time -- while only the DRAW POSITION moves. Clearing Wheel of Change's
    // bit (bit 5) must remove id 17 from both acts' lists, at two different
    // positions.
    uint16_t pool[kShrineListCount + kSpecialListCount];

    RunState a1 = fresh_act_state(1);
    a1.gold = 0;
    a1.special_membership = 0;
    a1.shrine_membership =
        static_cast<uint8_t>(a1.shrine_membership & ~(1u << 5));
    ASSERT_EQ(build_shrine_pool(a1, pool, 20), 5);
    const uint16_t want_a1[5] = {12, 13, 14, 15, 16};  // 17 was LAST
    for (int i = 0; i < 5; ++i) EXPECT_EQ(pool[i], want_a1[i]) << "i " << i;

    RunState a2 = fresh_act_state(2);
    a2.gold = 0;
    a2.special_membership = 0;
    a2.shrine_membership =
        static_cast<uint8_t>(a2.shrine_membership & ~(1u << 5));
    ASSERT_EQ(build_shrine_pool(a2, pool, 20), 5);
    const uint16_t want_a2[5] = {12, 13, 14, 15, 16};  // 17 was SECOND
    for (int i = 0; i < 5; ++i) EXPECT_EQ(pool[i], want_a2[i]) << "i " << i;

    // ... and the same bit in the other direction: keep ONLY Wheel of Change.
    a2.shrine_membership = static_cast<uint8_t>(1u << 5);
    ASSERT_EQ(build_shrine_pool(a2, pool, 20), 1);
    EXPECT_EQ(pool[0], 17u);
}

TEST(S213ActPools, TheInitSplitLeavesTheOneTimePoolAlone) {
    // reinit_act_event_pools is the act-crossing half; init_event_pools is the
    // run_begin whole. The difference IS the deliverable.
    RunState rs = fresh_run_state(kSeed, 20);
    rs.special_membership = 0x21u;  // an arbitrary punched-out pattern
    rs.event_membership = 0u;
    rs.shrine_membership = 0u;
    rs.act = 2;
    reinit_act_event_pools(rs);
    EXPECT_EQ(rs.special_membership, 0x21u) << "the special pool must not move";
    EXPECT_EQ(rs.event_membership, (1u << 13) - 1u);
    EXPECT_EQ(rs.shrine_membership, 0x3Fu);

    // The run_begin path DOES refill it.
    init_event_pools(rs);
    EXPECT_NE(rs.special_membership, 0x21u);
}

TEST(S213ActPools, ReinitReadsTheCurrentActNotThePrevious) {
    // The second failure mode the split invites: calling the rebuild BEFORE
    // act_transition bumps rs.act installs the previous act's width. Pinned by
    // showing the width tracks rs.act and nothing else.
    RunState rs = fresh_run_state(kSeed, 20);
    for (int act : {1, 2, 3}) {
        rs.act = static_cast<uint8_t>(act);
        reinit_act_event_pools(rs);
        EXPECT_EQ(rs.event_membership, (1u << event_list_count(act)) - 1u)
            << "act " << act;
    }
}

// --- B. the act-gated one-time draw filters ----------------------------------

TEST(S213DrawGates, Designer) {
    // `(TheCity || TheBeyond) && gold >= 75` (AbstractDungeon.java:1894-1898).
    for (int act : {2, 3}) {
        SCOPED_TRACE("act " + std::to_string(act));
        RunState rs = only_special(act, 20);
        rs.gold = 74;
        EXPECT_FALSE(special_offered(rs, 20)) << "gold 74 must skip";
        rs.gold = 75;
        EXPECT_TRUE(special_offered(rs, 20)) << "gold 75 is inclusive";
    }
    RunState a1 = only_special(1, 20);
    a1.gold = 9999;
    EXPECT_FALSE(special_offered(a1, 20)) << "Exordium is excluded at any gold";
}

TEST(S213DrawGates, Duplicator) {
    // `TheCity || TheBeyond`, no second clause (:1899-1903).
    EXPECT_FALSE(special_offered(only_special(1, 21), 21));
    EXPECT_TRUE(special_offered(only_special(2, 21), 21));
    EXPECT_TRUE(special_offered(only_special(3, 21), 21));
}

TEST(S213DrawGates, FaceTraderIsActsOneAndTwoAndNotActThree) {
    // `TheCity || Exordium` (:1904-1908) -- the one row whose act set EXCLUDES
    // Act 3 rather than including it, and the reason a blanket "acts 2-3"
    // reading of the special gates is wrong.
    EXPECT_TRUE(special_offered(only_special(1, 22), 22));
    EXPECT_TRUE(special_offered(only_special(2, 22), 22));
    EXPECT_FALSE(special_offered(only_special(3, 22), 22));
}

TEST(S213DrawGates, KnowingSkull) {
    // `TheCity && currentHealth > 12` (:1909-1913) -- STRICT, so 12 is out.
    RunState rs = only_special(2, 24);
    rs.hp = 12;
    EXPECT_FALSE(special_offered(rs, 24)) << "hp 12 is excluded (> 12, strict)";
    rs.hp = 13;
    EXPECT_TRUE(special_offered(rs, 24));
    for (int act : {1, 3}) {
        RunState other = only_special(act, 24);
        other.hp = 60;
        EXPECT_FALSE(special_offered(other, 24)) << "act " << act;
    }
}

TEST(S213DrawGates, NlothIsActTwoOnlyDespiteTheDuplicatedTest) {
    // `!id.equals("TheCity") && !id.equals("TheCity") || relics.size() < 2`
    // (:1914-1918). The SAME test is written twice -- one test, not two -- so
    // this is Act 2 only, NOT "any act but 2". S2.02's act_mask 0x2 agrees.
    RunState rs = only_special(2, 26);
    rs.relic_count = 1;
    EXPECT_FALSE(special_offered(rs, 26)) << "one relic is short of 2";
    rs.relic_count = 2;
    EXPECT_TRUE(special_offered(rs, 26));
    for (int act : {1, 3}) {
        RunState other = only_special(act, 26);
        other.relic_count = 5;
        EXPECT_FALSE(special_offered(other, 26)) << "act " << act;
    }
}

TEST(S213DrawGates, TheJoust) {
    // `TheCity && gold >= 50` (:1919-1923).
    RunState rs = only_special(2, 29);
    rs.gold = 49;
    EXPECT_FALSE(special_offered(rs, 29));
    rs.gold = 50;
    EXPECT_TRUE(special_offered(rs, 29));
    for (int act : {1, 3}) {
        RunState other = only_special(act, 29);
        other.gold = 9999;
        EXPECT_FALSE(special_offered(other, 29)) << "act " << act;
    }
}

TEST(S213DrawGates, SecretPortalIsPinnedFalseInEveryActIncludingTheBeyond) {
    // Design trap 5. `playtime >= 800.0f && TheBeyond` (:1929-1933). The ACT
    // HALF IS SATISFIED IN ACT 3, so from S2.13 on the `false` is carried
    // SOLELY by the unmodelled wall-clock playtime -- a real behavioural
    // deviation, not an act exclusion. This test exists so that anyone who
    // "fixes" the act gate alone fails here rather than shipping a portal.
    for (int act : {1, 2, 3}) {
        RunState rs = only_special(act, 28);
        rs.gold = 9999;
        rs.hp = rs.max_hp;
        rs.relic_count = 5;
        EXPECT_FALSE(special_offered(rs, 28))
            << "act " << act << ": SecretPortal must stay unreachable";
    }
}

TEST(S213DrawGates, ActIndependentSpecialsAreOfferedInEveryAct) {
    // The Woman in Blue (gold >= 50) and Fountain of Cleansing (isCursed) have
    // NO act test (:1924-1928, :1889-1893); the ungated rows -- Accursed
    // Blacksmith, Bonfire Elementals, Lab, WeMeetAgain -- fall to the
    // unconditional add (:1935).
    for (int act : {1, 2, 3}) {
        SCOPED_TRACE("act " + std::to_string(act));
        for (uint16_t id : {uint16_t{18}, uint16_t{19}, uint16_t{25},
                            uint16_t{30}}) {
            EXPECT_TRUE(special_offered(only_special(act, id), id))
                << "ungated id " << id;
        }
        RunState blue = only_special(act, 31);
        blue.gold = 49;
        EXPECT_FALSE(special_offered(blue, 31));
        blue.gold = 50;
        EXPECT_TRUE(special_offered(blue, 31));

        RunState fountain = only_special(act, 23);
        EXPECT_FALSE(special_offered(fountain, 23)) << "uncursed";
        add_card(fountain, CardId::CLUMSY);
        EXPECT_TRUE(special_offered(fountain, 23)) << "cursed";
    }
}

// --- C. the three Act-2/3 event gates ----------------------------------------

TEST(S213EventGates, BeggarNeedsSeventyFiveGold) {
    // `gold < 75 -> skip` (:1970-1973). s2-design 2.3 omitted this gate; S2.02
    // authored the registry row from source and this is its engine twin.
    RunState rs = fresh_act_state(2);
    rs.gold = 74;
    EXPECT_FALSE(event_offered(rs, 34));
    rs.gold = 75;
    EXPECT_TRUE(event_offered(rs, 34)) << "75 is inclusive";
}

TEST(S213EventGates, ColosseumNeedsRowEightOrDeeper) {
    // `currMapNode == null || currMapNode.y <= map.size() / 2 -> skip`
    // (:1975-1978). map.size() is the ROW COUNT (15), so the integer divide is
    // 7 and the gate is row >= 8. currMapNode is set at :1783, BEFORE
    // EventRoom.onPlayerEntry, so y is the ARRIVING node's row. In Act 2 the
    // boundary is floor 25 vs 26 (act_floor_base(2) == 17).
    RunState rs = fresh_act_state(2);
    rs.gold = 999;
    rs.floor = 17 + 8;  // row 7
    EXPECT_EQ(event_map_row(rs), 7);
    EXPECT_FALSE(event_offered(rs, 35)) << "row 7 is excluded (<= 7)";
    rs.floor = 17 + 9;  // row 8
    EXPECT_EQ(event_map_row(rs), 8);
    EXPECT_TRUE(event_offered(rs, 35)) << "row 8 is the first eligible row";

    // The `currMapNode == null` half: the pre-first-pick sentinel is row -1.
    rs.floor = 17;
    EXPECT_EQ(event_map_row(rs), -1);
    EXPECT_FALSE(event_offered(rs, 35));
}

TEST(S213EventGates, EventMapRowAgreesWithRunCurRow) {
    // event_map_row restates run_cur_row's arithmetic over RunState alone
    // (run_advance.hpp includes event_framework.hpp, not the reverse). If the
    // two ever drift, Colosseum's gate silently moves; pin them equal across
    // every act and every floor either could see. S2.33 inherits this gate.
    RunController rc{};
    for (int act : {1, 2, 3}) {
        rc.run.act = static_cast<uint8_t>(act);
        for (int floor = 0; floor <= 55; ++floor) {
            rc.run.floor = static_cast<uint16_t>(floor);
            EXPECT_EQ(event_map_row(rc.run), run_cur_row(rc))
                << "act " << act << " floor " << floor;
        }
    }
}

TEST(S213EventGates, MoaiHeadTakesTheIdolOrTheHalfHpArm) {
    // `!hasRelic("Golden Idol") && (float)hp / (float)maxHp > 0.5f -> skip`
    // (:1960-1963). A FLOAT divide against 0.5f, written as the Java writes it
    // rather than as hp*2 <= maxHp (trap 19).
    RunState rs = fresh_act_state(3);
    rs.max_hp = 100;

    rs.hp = 51;
    EXPECT_FALSE(event_offered(rs, 47)) << "51 % without the idol";
    rs.hp = 50;
    EXPECT_TRUE(event_offered(rs, 47)) << "exactly 50 % passes (<= 0.5f)";

    // The idol short-circuits the ratio entirely, at full HP.
    rs.hp = 100;
    EXPECT_FALSE(event_offered(rs, 47));
    rs.relics[rs.relic_count].relic_id =
        static_cast<uint16_t>(RelicId::GOLDEN_IDOL);
    ++rs.relic_count;
    EXPECT_TRUE(event_offered(rs, 47)) << "Golden Idol at full HP";
}

TEST(S213EventGates, ActOneEventGatesAreUnchanged) {
    // The Act-1 half must be bit-for-bit what it was before S2.13: the same
    // nine rows at floor 1 / gold 99, and none of the three new gates can
    // reach an Exordium id.
    RunState rs = fresh_run_state(kSeed, 20);
    rs.floor = 1;
    rs.gold = 99;
    uint16_t pool[kEventListMaxCount];
    const int n = build_event_pool(rs, pool, kEventListMaxCount);
    ASSERT_EQ(n, 9);
    const uint16_t want[9] = {1, 2, 4, 5, 6, 7, 8, 10, 11};
    for (int i = 0; i < 9; ++i) EXPECT_EQ(pool[i], want[i]) << "i " << i;
}

// --- D. cross-act depletion, end to end --------------------------------------

TEST(S213CrossAct, AnActOneSpecialDrawStaysRemovedInActTwo) {
    // The Acceptance's named case (trap 7). specialOneTimeEventList is handed
    // to the new dungeon by identity (CardCrawlGame.java:1102-1119), so a draw
    // in Act 1 is gone for the rest of the run.
    RunState rs = fresh_run_state(kSeed, 20);
    // Empty the shrine AND event lists so Lab is the only reachable id down
    // EITHER branch of the shrine/event split -- directly through getShrine,
    // or through getEvent's empty-tmp fallback into it.
    rs.shrine_membership = 0;
    rs.event_membership = 0;
    rs.special_membership =
        static_cast<uint16_t>(1u << (25 - kSpecialListFirstId));
    const uint16_t drawn = generate_event(rs);
    ASSERT_EQ(drawn, 25u) << "Lab was the only thing in the pool";
    EXPECT_EQ(rs.special_membership, 0u);
    EXPECT_TRUE(event_flag_test(rs, 25));

    // Cross to Act 2 the way act_transition does.
    rs.act = 2;
    reinit_act_event_pools(rs);
    EXPECT_EQ(rs.special_membership, 0u) << "Lab must NOT come back";
    EXPECT_FALSE(special_offered(rs, 25));
    EXPECT_TRUE(event_flag_test(rs, 25)) << "and it is still recorded as fired";
}

TEST(S213CrossAct, AnActOneShrineDrawReturnsInActTwo) {
    // The counterpart the ledger's "cross-act depletion" framing does NOT
    // cover. shrineList.clear() (:2577) + initializeShrineList (:293) put all
    // six back, every act.
    RunState rs = fresh_run_state(kSeed, 20);
    rs.special_membership = 0;
    rs.event_membership = 0;  // so either branch of the split reaches getShrine
    rs.shrine_membership =
        static_cast<uint8_t>(1u << (14 - kShrineListFirstId));
    const uint16_t drawn = generate_event(rs);
    ASSERT_EQ(drawn, 14u) << "Transmogrifier was the only thing in the pool";
    EXPECT_EQ(rs.shrine_membership, 0u);

    rs.act = 2;
    reinit_act_event_pools(rs);
    EXPECT_EQ(rs.shrine_membership, 0x3Fu);
    rs.special_membership = 0;
    EXPECT_TRUE(special_offered(rs, 14)) << "the shrine is drawable again";
}

TEST(S213CrossAct, ActThreeEventPoolExhaustionFallsToGetShrine) {
    // getEvent's empty-filtered-tmp fallback into getShrine (:1983-1985) -- a
    // THIRD draw off the throwaway stream. Ordinary rather than exotic in Act
    // 3, whose event list is only seven rows across ~15 map rows.
    RunState rs = fresh_act_state(3);
    rs.event_membership = 0;  // every Beyond event already drawn
    rs.special_membership = 0;
    rs.shrine_membership =
        static_cast<uint8_t>(1u << (16 - kShrineListFirstId));
    const RngStream before = rs.event_rng;

    // Whichever branch the split draw takes, the only reachable id is 16 --
    // through getShrine directly, or through getEvent's empty-tmp fallback.
    const uint16_t drawn = generate_event(rs);
    EXPECT_EQ(drawn, 16u);
    EXPECT_TRUE(streams_equal(before, rs.event_rng))
        << "the selection stream is a throwaway and must not commit";
    EXPECT_EQ(rs.shrine_membership, 0u);
}

TEST(S213CrossAct, ARawNonemptyButFilteredEmptyShrinePoolReturnsZero) {
    // The documented defensive deviation, now on a REACHABLE Act-3 state
    // rather than a structurally impossible one: shrineList empty (all six
    // drawn within Act 3 -- they are restored at every crossing, so this is
    // an intra-act exhaustion) and every surviving special filtered out. The
    // game would evaluate tmp.get(rng.random(-1)) (:1937) and crash after
    // burning one counter tick; this port returns 0 without drawing.
    RunState rs = fresh_act_state(3);
    rs.event_membership = 0;
    rs.shrine_membership = 0;
    // FaceTrader (acts 1-2) and SecretPortal (pinned false) both survive in
    // the RAW list and both filter out in Act 3.
    const uint16_t raw =
        static_cast<uint16_t>((1u << (22 - kSpecialListFirstId)) |
                              (1u << (28 - kSpecialListFirstId)));
    rs.special_membership = raw;
    uint16_t pool[kShrineListCount + kSpecialListCount];
    ASSERT_EQ(build_shrine_pool(rs, pool, 20), 0)
        << "the FILTERED list must be empty while the RAW list is not";

    const RngStream before = rs.event_rng;
    EXPECT_EQ(generate_event(rs), 0u);
    EXPECT_TRUE(streams_equal(before, rs.event_rng));
    EXPECT_EQ(rs.special_membership, raw)
        << "nothing may be removed on the no-draw path";
}

// --- E. the two-word FIRED bitset --------------------------------------------

TEST(S213EventFlags, EveryIdOneToFiftyOneRoundTripsThroughItsOwnWord) {
    for (uint16_t id = 1; id <= 51; ++id) {
        SCOPED_TRACE("id " + std::to_string(id));
        RunState rs{};
        EXPECT_FALSE(event_flag_test(rs, id)) << "value-init must read clean";
        event_flag_set(rs, id);
        EXPECT_TRUE(event_flag_test(rs, id));
        if (id <= 31) {
            EXPECT_EQ(rs.event_flags, 1u << (id - 1u));
            EXPECT_EQ(rs.event_flags_hi, 0u) << "ids 1..31 must not touch hi";
        } else {
            // Bit (id - 32): id 32 is bit 0. The pin deliberately does NOT
            // mirror the accessor's expression -- the (id - 33u) era had this
            // line share the accessor's underflow at id 32, so both sides
            // wrapped identically and the round trip stayed green over UB.
            EXPECT_EQ(rs.event_flags_hi, 1u << (id - 32u));
            EXPECT_EQ(rs.event_flags, 0u) << "ids 32..63 must not touch lo";
        }
        // ... and only that id reads set.
        for (uint16_t other = 1; other <= 51; ++other) {
            if (other == id) continue;
            EXPECT_FALSE(event_flag_test(rs, other))
                << "id " << id << "'s bit also read as " << other;
        }
    }
}

TEST(S213EventFlags, OutOfRangeIdsAreNoOpsRatherThanUbShifts) {
    RunState rs{};
    for (uint16_t id : {uint16_t{0}, uint16_t{64}, uint16_t{100},
                        uint16_t{0xFFFF}}) {
        event_flag_set(rs, id);
        EXPECT_FALSE(event_flag_test(rs, id)) << "id " << id;
    }
    EXPECT_EQ(rs.event_flags, 0u);
    EXPECT_EQ(rs.event_flags_hi, 0u);
    // An all-ones pair still reports false for an id with no bit at all.
    rs.event_flags = 0xFFFFFFFFu;
    rs.event_flags_hi = 0xFFFFFFFFu;
    EXPECT_FALSE(event_flag_test(rs, 0));
    EXPECT_FALSE(event_flag_test(rs, 64));
    // ids 52..63 are addressable-but-unallocated, which is the headroom a
    // future registry id needs; 64 upward would need a THIRD word, not a
    // wider shift.
    EXPECT_TRUE(event_flag_test(rs, 52));
}

TEST(S213EventFlags, AnActTwoDrawSetsTheHiWordAndLeavesTheLoWordAlone) {
    // The end-to-end reason the second word exists: before S2.13 an Act-2 draw
    // would have written `1u << (id-1)` with id up to 44 -- unrepresentable in
    // a uint32_t, and UB at id 32 and beyond.
    RunState rs = fresh_act_state(2);
    rs.shrine_membership = 0;
    rs.special_membership = 0;
    rs.gold = 999;
    rs.event_membership =
        static_cast<uint16_t>(1u << (41 - event_list_first_id(2)));
    const uint16_t drawn = generate_event(rs);
    ASSERT_EQ(drawn, 41u) << "Nest was the only thing in the pool";
    EXPECT_EQ(rs.event_flags, 0u);
    EXPECT_EQ(rs.event_flags_hi, 1u << (41u - 32u));
    EXPECT_TRUE(event_flag_test(rs, 41));
    EXPECT_EQ(rs.event_membership, 0u);
}

// --- F. the end state S2.31-S2.33 inherit -------------------------------------

TEST(S213CrossAct, AnActTwoQuestionMarkRoomSelectsACityRowAndCommitsIt) {
    // After S2.13 an Act-2 ? room that resolves to EVENT selects the RIGHT id
    // and commits the right pool bit and the right flag word. Whether the id
    // then opens a dialog or parks depends on which body batch has landed --
    // S2.32's five City rows dispatch, S2.31's eight park until their batch --
    // so this test pins the SELECTION commit and checks the dispatch answer
    // against the registry's own implemented column rather than a hardcoded
    // "all park" claim.
    RunState rs = fresh_act_state(2);
    rs.gold = 999;
    rs.floor = 17 + 9;
    rs.special_membership = 0;
    rs.shrine_membership = 0;

    const uint16_t drawn = generate_event(rs);
    ASSERT_NE(drawn, 0u);
    EXPECT_GE(drawn, 32u);
    EXPECT_LE(drawn, 44u) << "an Act-2 event draw must be a TheCity row";
    EXPECT_TRUE(event_flag_test(rs, drawn));
    const unsigned bit = static_cast<unsigned>(drawn - event_list_first_id(2));
    EXPECT_EQ((static_cast<unsigned>(rs.event_membership) >> bit) & 1u, 0u)
        << "the drawn row must leave the pool";
    const sts::registry::EventDef* def =
        sts::registry::event_def(static_cast<EventId>(drawn));
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(event_dialog_impl(drawn) != nullptr, def->implemented)
        << "dispatch and the registry's implemented column disagree for id "
        << drawn;
}

}  // namespace
}  // namespace sts::engine
