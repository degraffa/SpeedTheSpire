// S2.12 -- the act transition (act 1 -> 2 -> 3) and Acts 2-3 map generation.
//
// The properties under test, in the order the ledger's Acceptance names them:
//   * TRAP 1, the cardRng COUNTER SNAP: three strictly-open bands, so a counter
//     of exactly 0 / 250 / 500 / 750 does NOT move, there is no fourth band, and
//     the advance is setCounter's randomBoolean loop -- N raw next_long() steps,
//     provably not a replay of random(999).
//   * THE A5 HEAL arithmetic (75 % of MISSING, MathUtils.round half-up) versus
//     the full heal below A5, and the onPlayerHeal pass around it.
//   * WHAT RESETS VERSUS WHAT CARRIES: ?-room pity and blizzardPotionMod reset;
//     cardBlizzRandomizer, the relic pools, the one-time event pool and every
//     run-lifetime stream carry. That pair is the easiest thing here to get
//     backwards, so both halves are pinned side by side.
//   * mapRng OFFSETS per act (+1 / +200 / +600) and setEmeraldElite's one draw
//     per GENERATED act.
//   * FLOOR CONTINUITY -- the deferred-obligations pair. 17/34 are the floors at
//     which Acts 2/3 are CONSTRUCTED; 18/35 are their first playable rooms.
//   * THE ACT-DEPENDENT LIST GENERATION: two weak draws, not three, so the
//     monster list is 15 long in Acts 2-3; the Act-3 "3 Darklings" self-
//     exclusion; one randomLong per act for the boss shuffle.
//   * A THREE-ACT SIM under a scripted policy, twice, with identical per-step
//     hash chains.
//
// Every expected value is hand-carried from the decompiled source read in full
// for this task -- AbstractDungeon.java:268-308 / 2562-2604 / 1687-1813,
// TheCity.java:37-182, TheBeyond.java:35-176, MainMusic.java:46-90,
// Random.java:42-51, AbstractCreature.java:386-417, AbstractRoom.java:277-357 --
// and not read back from the engine.

#include "sts/engine/run_advance.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/map_gen.hpp"
#include "sts/engine/map_rooms.hpp"
#include "sts/engine/neow.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"

namespace sts::engine {
namespace {

constexpr int64_t kSeed = 12345;
constexpr uint8_t kA20 = 20;

void step(RunController& rc, Action a) {
    StepResult res{};
    advance(std::span<RunController>(&rc, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&res, 1));
}

StepResult step_with_result(RunController& rc, Action a) {
    StepResult res{};
    advance(std::span<RunController>(&rc, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&res, 1));
    return res;
}

RunActionMask mask_of(const RunController& rc) {
    RunActionMask m{};
    legal_actions(rc, m);
    return m;
}

// A byte hash of the WHOLE controller. RunController is trivially copyable and
// every alignment gap in it (and in RunState / CombatState / the transient
// screen structs) is a declared member -- that is exactly what byte_class.hpp's
// tiling tripwire enforces -- so hashing its object representation is stable
// across hosts and optimisation levels. FNV-1a rather than a dependency: this is
// an equality witness for two runs in one process, not a persisted digest.
uint64_t hash_controller_bytes(const RunController& rc) {
    unsigned char buf[sizeof(RunController)];
    std::memcpy(buf, &rc, sizeof buf);
    uint64_t h = 1469598103934665603ull;
    for (unsigned char b : buf) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}

// Park a run at the boss chest of `act` WITHOUT walking the act's rooms, at the
// act's REAL chest floor -- which is what the floor-scoped streams and the BGM
// draw observe, so it cannot be faked at floor 1. The edge taken is the public
// one the boss reward screen's proceed takes.
void place_at_boss_chest(RunController& rc) {
    rc.run.floor = static_cast<uint16_t>(
        act_floor_base(static_cast<int>(rc.run.act)) + kActFloorSpan - 1);
    rc.cur_x = static_cast<uint8_t>(kBossCol);
    rc.room_type = static_cast<uint8_t>(RoomType::Boss);
    rc.combat_outcome = static_cast<uint8_t>(RunCombatOutcome::KILLED);
    rc.phase = static_cast<uint8_t>(RunPhase::COMBAT_REWARD);
    next_room_transition_boss_chest(rc);  // ++floor -> the chest floor
}

// A run standing in the Act-1 boss chest at floor 17, one Proceed away from
// Act 2.
RunController at_act1_boss_chest(int64_t seed = kSeed, uint8_t asc = kA20) {
    RunController rc = run_begin(seed, asc);
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::DONE);
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    place_at_boss_chest(rc);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::BOSS_TREASURE));
    EXPECT_EQ(rc.run.floor, 17);
    return rc;
}

// Cross the boundary the way a player does: Proceed on an unopened chest.
void cross_act(RunController& rc) {
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::BOSS_TREASURE));
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
}

// =============================================================================
// TRAP 1 -- the cardRng counter snap (AbstractDungeon.java:2564-2570)
// =============================================================================

TEST(CardRngSnap, BandsRoundUpToNextMultipleOf250) {
    EXPECT_EQ(card_rng_snap_target(1), 250);
    EXPECT_EQ(card_rng_snap_target(3), 250);
    EXPECT_EQ(card_rng_snap_target(249), 250);
    EXPECT_EQ(card_rng_snap_target(251), 500);
    EXPECT_EQ(card_rng_snap_target(499), 500);
    EXPECT_EQ(card_rng_snap_target(501), 750);
    EXPECT_EQ(card_rng_snap_target(749), 750);
}

TEST(CardRngSnap, ExactBoundariesDoNotSnap) {
    // Every band is `counter > lo && counter < hi`, so each boundary fails its
    // own band's `<` AND the next band's `>`. A "round up to the next multiple
    // of 250" implementation gets three of these four wrong and looks right.
    EXPECT_EQ(card_rng_snap_target(0), 0) << "0 is not > 0";
    EXPECT_EQ(card_rng_snap_target(250), 250);
    EXPECT_EQ(card_rng_snap_target(500), 500);
    EXPECT_EQ(card_rng_snap_target(750), 750);
}

TEST(CardRngSnap, AboveSevenFiftyIsUnchanged) {
    // There is no fourth band -- :2568-2570 is the last `else if`.
    EXPECT_EQ(card_rng_snap_target(751), 751);
    EXPECT_EQ(card_rng_snap_target(1000), 1000);
    EXPECT_EQ(card_rng_snap_target(123456), 123456);
}

TEST(CardRngSnap, AdvanceIsExactlyNRawNextLongSteps) {
    // setCounter replays randomBoolean() (Random.java:42-51), which is
    // RandomXS128.nextBoolean -> ONE raw nextLong(). The advance is therefore
    // exactly (target - counter) engine steps, and this pins that against an
    // explicit hand-written replay rather than against the implementation.
    RngStream a = from_seed(kSeed);
    for (int i = 0; i < 7; ++i) (void)random(a, 42);
    RngStream b = a;

    advance_counter_to(a, 250);
    for (int i = 0; i < 250 - 7; ++i) (void)random_boolean(b);

    EXPECT_EQ(a.counter, 250);
    EXPECT_EQ(b.counter, 250);
    EXPECT_EQ(a.s0, b.s0);
    EXPECT_EQ(a.s1, b.s1);

    // AND THE CORRECTION THE S2 SCOUT DOSSIER GOT WRONG, recorded as an
    // assertion so nobody re-derives it the hard way. The dossier claimed a
    // replay of random(999) would reach the right counter and the WRONG engine
    // state. It does not, and rng_stream.hpp's own from_seed_counter /
    // from_seed_set_counter pair already said so: the xorshift128+ state
    // advance is a function of the NUMBER of nextLong() calls alone, never of
    // the value consumed, and nextInt(1000)'s rejection loop
    // (`bits - value + (n-1) < 0`) retries with probability ~1e-16 per call. So
    // the two replays coincide, and "the state is wrong" is not what makes
    // randomBoolean the right implementation -- being what the Java writes is.
    RngStream replay = from_seed(kSeed);
    for (int i = 0; i < 7; ++i) (void)random(replay, 42);
    for (int i = 0; i < 250 - 7; ++i) (void)random(replay, 999);
    EXPECT_EQ(replay.counter, 250);
    EXPECT_EQ(replay.s0, a.s0)
        << "one wrapper draw is one nextLong whatever the wrapper -- if this "
           "ever fails, the one-draw invariant broke, not the snap";
    EXPECT_EQ(replay.s1, a.s1);

    // What a WRONG implementation actually looks like, and the discriminating
    // property: a snap that advanced by the wrong NUMBER of steps.
    RngStream off_by_one = from_seed(kSeed);
    for (int i = 0; i < 7; ++i) (void)random(off_by_one, 42);
    for (int i = 0; i < 250 - 7 - 1; ++i) (void)random_boolean(off_by_one);
    EXPECT_TRUE(off_by_one.s0 != a.s0 || off_by_one.s1 != a.s1);
}

TEST(CardRngSnap, TargetAtOrBelowTheCounterIsANoOp) {
    // Random.setCounter's else-branch just logs (:48-50).
    RngStream s = from_seed(kSeed);
    for (int i = 0; i < 900; ++i) (void)random_boolean(s);
    const RngStream before = s;
    advance_counter_to(s, card_rng_snap_target(s.counter));
    EXPECT_EQ(std::memcmp(&before, &s, sizeof before), 0);
}

TEST(CardRngSnap, TheCrossingAppliesItToTheLiveCardRng) {
    RunController rc = at_act1_boss_chest();
    // Put the stream inside the first band; anything from a live Act-1 run is
    // well under 250 anyway, so this only makes the expectation exact.
    while (rc.run.card_rng.counter < 5) (void)random(rc.run.card_rng, 3);
    RngStream expected = rc.run.card_rng;
    advance_counter_to(expected, 250);

    cross_act(rc);

    EXPECT_EQ(rc.run.card_rng.counter, 250);
    EXPECT_EQ(rc.run.card_rng.s0, expected.s0);
    EXPECT_EQ(rc.run.card_rng.s1, expected.s1);
}

// =============================================================================
// The A5 heal (AbstractDungeon.java:2582-2586)
// =============================================================================

TEST(ActTransition, HealIsFullBelowAscensionFive) {
    // The argument is maxHealth, which the unguarded += and the clamp inside
    // AbstractCreature.heal turn into a full heal whatever is missing.
    EXPECT_EQ(act_transition_heal_amount(80, 1, 4), 80);
    EXPECT_EQ(act_transition_heal_amount(80, 80, 0), 80);
}

TEST(ActTransition, HealIsSeventyFivePercentOfMissingAtAscensionFive) {
    // MathUtils.round is half-up on the double-widened float, so 0.75 -> 1 and
    // 1.5 -> 2: at 1 HP missing the player heals 1, not 0.
    EXPECT_EQ(act_transition_heal_amount(80, 79, 5), 1);   // 1  * 0.75 = 0.75
    EXPECT_EQ(act_transition_heal_amount(80, 78, 5), 2);   // 2  * 0.75 = 1.5
    EXPECT_EQ(act_transition_heal_amount(80, 77, 5), 2);   // 3  * 0.75 = 2.25
    EXPECT_EQ(act_transition_heal_amount(75, 1, 20), 56);  // 74 * 0.75 = 55.5
    EXPECT_EQ(act_transition_heal_amount(80, 80, 20), 0);  // nothing missing
}

TEST(ActTransition, TheCrossingAppliesTheAscensionFiveHeal) {
    RunController rc = at_act1_boss_chest();
    rc.run.hp = 1;
    const int expected =
        1 + act_transition_heal_amount(static_cast<int>(rc.run.max_hp), 1, kA20);
    cross_act(rc);
    EXPECT_EQ(rc.run.hp, expected);
    EXPECT_LT(rc.run.hp, rc.run.max_hp) << "A5+ heals a fraction, not to full";
}

TEST(ActTransition, TheCrossingHealsToFullBelowAscensionFive) {
    RunController rc = at_act1_boss_chest(kSeed, /*asc=*/0);
    rc.run.hp = 1;
    cross_act(rc);
    EXPECT_EQ(rc.run.hp, rc.run.max_hp);
}

TEST(ActTransition, MarkOfTheBloomZeroesTheTransitionHeal) {
    // MarkOfTheBloom.onPlayerHeal (MarkOfTheBloom.java:25-29) ignores its
    // argument and returns 0 -- an absolute suppressor, not a modifier. The
    // relic is granted by an event body S2.33 owns; the SEAM is what is pinned
    // here, planted directly into the relic array.
    RunController rc = at_act1_boss_chest();
    rc.run.hp = 1;
    RelicSlot& slot = rc.run.relics[rc.run.relic_count++];
    slot = RelicSlot{};
    slot.relic_id = static_cast<uint16_t>(RelicId::MARK_OF_THE_BLOOM);
    slot.counter = -1;

    cross_act(rc);
    EXPECT_EQ(rc.run.hp, 1) << "the whole between-act heal is cancelled";
}

TEST(ActTransition, MagicFlowerDoesNotApplyOutsideCombat) {
    // MagicFlower.onPlayerHeal is gated on RoomPhase.COMBAT (MagicFlower.java:
    // 30-37) and the crossing is not in a combat, so the x1.5 must NOT appear.
    RunController rc = at_act1_boss_chest();
    rc.run.hp = static_cast<int16_t>(rc.run.max_hp - 8);
    RelicSlot& slot = rc.run.relics[rc.run.relic_count++];
    slot = RelicSlot{};
    slot.relic_id = static_cast<uint16_t>(RelicId::MAGIC_FLOWER);
    slot.counter = -1;

    const int expected = static_cast<int>(rc.run.hp) +
                         act_transition_heal_amount(
                             static_cast<int>(rc.run.max_hp),
                             static_cast<int>(rc.run.hp), kA20);
    cross_act(rc);
    EXPECT_EQ(rc.run.hp, expected);
}

// =============================================================================
// What resets, and what carries (s2-design §4.2's ledger)
// =============================================================================

TEST(ActTransition, EventPityResetsToBaseChances) {
    // EventHelper.resetProbabilities (EventHelper.java:189-195). ELITE_CHANCE's
    // reset to 0.0f -- NOT to the 0.1f the monster row ramps from -- has no
    // engine field because its only consumer is under a DeadlyEvents mod gate
    // (:190-192, :204-207); the other three are the modelled ones.
    RunController rc = at_act1_boss_chest();
    rc.run.event_pity_monster = 0.7f;
    rc.run.event_pity_shop = 0.21f;
    rc.run.event_pity_treasure = 0.14f;
    cross_act(rc);
    EXPECT_FLOAT_EQ(rc.run.event_pity_monster, kEventBaseMonsterChance);
    EXPECT_FLOAT_EQ(rc.run.event_pity_shop, kEventBaseShopChance);
    EXPECT_FLOAT_EQ(rc.run.event_pity_treasure, kEventBaseTreasureChance);
}

TEST(ActTransition, BlizzardPotionModResetsToZero) {
    RunController rc = at_act1_boss_chest();
    rc.run.blizzard_potion_mod = 40;
    cross_act(rc);
    EXPECT_EQ(rc.run.blizzard_potion_mod, 0);
}

TEST(ActTransition, CardBlizzRandomizerCarriesAcrossTheBoundary) {
    // Nothing in :2562-2604 touches it; it is reset only by reset() and by a
    // RARE reward roll (:1396/:1437). The pairing with the pity reset above is
    // the whole point -- one resets, its neighbour does not.
    RunController rc = at_act1_boss_chest();
    rc.run.card_blizz_randomizer = -17;
    cross_act(rc);
    EXPECT_EQ(rc.run.card_blizz_randomizer, -17);
}

TEST(ActTransition, RelicPoolsCarryDepleted) {
    // initializeRelicList is called from Exordium's constructor ONLY
    // (Exordium.java:38), never from the shared chain -- so the Act-2 boss chest
    // pops from wherever Act 1 left the pool.
    RunController rc = at_act1_boss_chest();
    std::vector<uint16_t> before[kRelicTierCount];
    for (int t = 0; t < kRelicTierCount; ++t) {
        before[t].assign(rc.run.relic_pools[t],
                         rc.run.relic_pools[t] + rc.run.relic_pool_count[t]);
    }
    cross_act(rc);
    for (int t = 0; t < kRelicTierCount; ++t) {
        std::vector<uint16_t> after(
            rc.run.relic_pools[t],
            rc.run.relic_pools[t] + rc.run.relic_pool_count[t]);
        EXPECT_EQ(after, before[t]) << "pool tier " << t;
    }
}

TEST(ActTransition, TheOneTimeEventPoolCarriesByReference) {
    // specialOneTimeEventList is handed to the new dungeon BY IDENTITY
    // (AbstractDungeon.java:280, getDungeon :1102-1119), so an Act-1 draw stays
    // removed for Acts 2-3 (trap 7). Carrying it is DOING NOTHING.
    RunController rc = at_act1_boss_chest();
    rc.run.special_membership = 0x2u;
    rc.run.event_membership = 0x5u;
    rc.run.shrine_membership = 0x3u;
    cross_act(rc);
    EXPECT_EQ(rc.run.special_membership, 0x2u);
    // S2.13: the OTHER two lists do the OPPOSITE, which is what makes this
    // test's name load-bearing rather than decorative. dungeonTransitionSetup
    // CLEARS eventList and shrineList (AbstractDungeon.java:2576-2577) and the
    // new dungeon's constructor REBUILDS both (:291, :293), so the punched-out
    // pattern is erased: the event mask returns at the NEW act's width
    // (TheCity's 13 rows) and every shrine returns to the pool. Only the
    // one-time pool carries, because only it is passed by identity.
    EXPECT_EQ(rc.run.event_membership, (1u << 13) - 1u);
    EXPECT_EQ(rc.run.shrine_membership, 0x3Fu);
}

TEST(ActTransition, ShrinesReturnToThePoolAtEveryCrossing) {
    // The counterpart nobody expects, and the single easiest thing in S2.13 to
    // get backwards. A shrine drawn in Act 1 is drawable AGAIN in Act 2 and
    // again in Act 3: shrineList.clear() (:2577) + initializeShrineList (:293)
    // restore all six, every time. Only specialOneTimeEventList depletes
    // run-wide (CardCrawlGame.java:1102-1119 hands the same object over).
    RunController rc = at_act1_boss_chest();
    rc.run.shrine_membership = 0u;   // every shrine drawn during Act 1
    rc.run.special_membership = 0u;  // every special drawn during Act 1
    cross_act(rc);
    EXPECT_EQ(rc.run.shrine_membership, 0x3Fu) << "shrines must come back";
    EXPECT_EQ(rc.run.special_membership, 0u) << "specials must NOT come back";
    // Act 2's event list is TheCity's thirteen rows (TheCity.java:185-198).
    EXPECT_EQ(rc.run.event_membership, (1u << 13) - 1u);

    // ... and it happens AGAIN into Act 3, on a state whose Act-2 shrines were
    // all drawn. The 2->3 crossing is driven directly rather than through a
    // second boss chest: cross_act needs a BOSS_TREASURE phase, and rebuilding
    // one here would test the chest, not the pools.
    rc.run.shrine_membership = 0u;
    StepResult res{};
    on_boss_chest_proceed(rc, rc.run, res);
    EXPECT_EQ(rc.run.act, 3u);
    EXPECT_EQ(rc.run.shrine_membership, 0x3Fu);
    EXPECT_EQ(rc.run.special_membership, 0u);
    // Act 3's event list is TheBeyond's seven rows (TheBeyond.java:178-187).
    EXPECT_EQ(rc.run.event_membership, (1u << 7) - 1u);
}

TEST(ActTransition, TheCrossingDoesNotRerunTheRunStartSpecialInit) {
    // The negative that keeps the init split honest. If act_transition ever
    // called init_event_pools instead of reinit_act_event_pools, the one-time
    // pool would be REFILLED at every act -- and at A15+ the Note For Yourself
    // bit, which the run-start path deliberately never sets, would appear.
    RunController rc = at_act1_boss_chest();
    rc.run.ascension = 15;  // note_for_yourself_available(15) == false
    rc.run.special_membership = 0u;
    cross_act(rc);
    EXPECT_EQ(rc.run.special_membership, 0u)
        << "the crossing refilled the one-time pool";
}

TEST(ActTransition, CardPoolsAreUnchangedAndConsumeNoRng) {
    // initializeCardPools (:294) is a pure rebuild from static libraries and
    // this engine's pools are constexpr, so the deliverable is the NEGATIVE:
    // the crossing moves no stream on their account. Every run-lifetime stream
    // except monsterRng (list generation) and cardRng (the snap) must be
    // byte-identical afterwards.
    RunController rc = at_act1_boss_chest();
    const RngStream event_before = rc.run.event_rng;
    const RngStream merchant_before = rc.run.merchant_rng;
    const RngStream treasure_before = rc.run.treasure_rng;
    const RngStream relic_before = rc.run.relic_rng;
    const RngStream potion_before = rc.run.potion_rng;
    const RngStream neow_before = rc.run.neow_rng;

    cross_act(rc);

    EXPECT_EQ(std::memcmp(&event_before, &rc.run.event_rng, sizeof event_before), 0);
    EXPECT_EQ(std::memcmp(&merchant_before, &rc.run.merchant_rng, sizeof merchant_before), 0);
    EXPECT_EQ(std::memcmp(&treasure_before, &rc.run.treasure_rng, sizeof treasure_before), 0);
    EXPECT_EQ(std::memcmp(&relic_before, &rc.run.relic_rng, sizeof relic_before), 0);
    EXPECT_EQ(std::memcmp(&potion_before, &rc.run.potion_rng, sizeof potion_before), 0);
    EXPECT_EQ(std::memcmp(&neow_before, &rc.run.neow_rng, sizeof neow_before), 0);
}

TEST(ActTransition, TheRunSetupAscensionBlockDoesNotRerun) {
    // :2590-2602 is gated `floorNum <= 1 && dungeon instanceof Exordium`; both
    // halves are false at every act boundary. If it ever ran again an A20 run
    // would lose 5 more max HP, be rewritten to 90 % of it, and gain a second
    // Ascender's Bane.
    RunController rc = at_act1_boss_chest();
    const int16_t max_hp_before = rc.run.max_hp;
    const uint16_t deck_before = rc.run.master_deck_count;
    cross_act(rc);
    EXPECT_EQ(rc.run.max_hp, max_hp_before);
    EXPECT_EQ(rc.run.master_deck_count, deck_before);
}

TEST(ActTransition, PurgeCostRampsRunWideAndIsNotReset) {
    // s2-design §5 trap 10's negative freeze: ShopScreen's purge cost resets
    // only in the new-run block (CardCrawlGame.java:478), never per act.
    RunController rc = at_act1_boss_chest();
    rc.run.purge_cost = 137;
    cross_act(rc);
    EXPECT_EQ(rc.run.purge_cost, 137);
}

// =============================================================================
// Streams and floors at the crossing
// =============================================================================

TEST(ActTransition, FloorNumIsUnchangedByTheCrossing) {
    // `isDungeonBeaten = true` (ProceedButton.java:249-250) is exactly what
    // makes updateFading skip nextRoomTransition (:2317-2326), so there is no
    // ++floorNum at the boundary. dungeonTransitionSetup resets it nowhere.
    RunController rc = at_act1_boss_chest();
    ASSERT_EQ(rc.run.floor, 17);
    cross_act(rc);
    EXPECT_EQ(rc.run.floor, 17);
    EXPECT_EQ(rc.run.act, 2);
}

TEST(ActTransition, FloorStreamsAreNotReseededAtTheCrossing) {
    // The five floor-scoped streams are reseeded only inside nextRoomTransition
    // (:1747-1751), which does not run here -- so they are still the floor-17
    // streams throughout the construction, and the BGM draw comes off that one.
    RunController rc = at_act1_boss_chest();
    const RngStream hp_before = rc.combat.monster_hp_rng;
    const RngStream ai_before = rc.combat.ai_rng;
    const RngStream shuffle_before = rc.combat.shuffle_rng;
    const RngStream card_random_before = rc.combat.card_random_rng;
    const RngStream misc_before = rc.combat.misc_rng;

    cross_act(rc);

    EXPECT_EQ(std::memcmp(&hp_before, &rc.combat.monster_hp_rng, sizeof hp_before), 0);
    EXPECT_EQ(std::memcmp(&ai_before, &rc.combat.ai_rng, sizeof ai_before), 0);
    EXPECT_EQ(std::memcmp(&shuffle_before, &rc.combat.shuffle_rng, sizeof shuffle_before), 0);
    EXPECT_EQ(std::memcmp(&card_random_before, &rc.combat.card_random_rng, sizeof card_random_before), 0);
    // miscRng is the ONE exception, and by exactly one draw -- the act BGM.
    EXPECT_EQ(rc.combat.misc_rng.counter, misc_before.counter + 1);
}

TEST(ActTransition, ActBgmDrawConsumesExactlyOneMiscRng) {
    // CardCrawlGame.music.changeBGM(id) -> new MainMusic(key) -> getSong, whose
    // TheCity and TheBeyond arms each draw miscRng.random(1)
    // (MainMusic.java:65-79; MusicMaster.changeBGM :126-128 is unconditional).
    // It sits AFTER generateMap in both constructors, so the expected state is
    // one random(1) off the un-reseeded floor stream.
    for (int act = 2; act <= 3; ++act) {
        RunController rc = at_act1_boss_chest();
        if (act == 3) {
            cross_act(rc);
            place_at_boss_chest(rc);
            ASSERT_EQ(rc.run.floor, 34);
        }
        RngStream expected = rc.combat.misc_rng;
        (void)random(expected, 1);

        cross_act(rc);

        ASSERT_EQ(rc.run.act, act);
        EXPECT_EQ(rc.combat.misc_rng.counter, expected.counter) << "act " << act;
        EXPECT_EQ(rc.combat.misc_rng.s0, expected.s0) << "act " << act;
        EXPECT_EQ(rc.combat.misc_rng.s1, expected.s1) << "act " << act;
    }
}

// =============================================================================
// The deferred-obligations row: the exact Act-2/3 entry floors
// =============================================================================
//
// The row asks for a PAIR per act, and conflating its two halves is the mistake
// it exists to prevent: 17/34 are the CONSTRUCTION floors (what
// dungeonTransitionSetup, the constructors, generateMap, setEmeraldElite and the
// BGM draw observe, and what the un-reseeded floor streams still carry) while
// 18/35 are the first PLAYABLE rooms.

TEST(ActFloors, TheSpanIsFifteenRoomsPlusBossPlusChest) {
    EXPECT_EQ(kActFloorSpan, 17);
    EXPECT_EQ(kMapRows, 15) << "15 map rows, one floor each";
    EXPECT_EQ(act_floor_base(1), 0);
    EXPECT_EQ(act_floor_base(2), 17);
    EXPECT_EQ(act_floor_base(3), 34);
}

TEST(ActFloors, BossIsSixteenThirtyThreeFiftyAndChestIsSeventeenThirtyFour) {
    EXPECT_EQ(act_floor_base(1) + kActFloorSpan - 1, 16);  // Act-1 boss
    EXPECT_EQ(act_floor_base(2), 17);                      // Act-1 chest
    EXPECT_EQ(act_floor_base(2) + kActFloorSpan - 1, 33);  // Act-2 boss
    EXPECT_EQ(act_floor_base(3), 34);                      // Act-2 chest
    EXPECT_EQ(act_floor_base(3) + kActFloorSpan - 1, 50);  // Act-3 boss
}

TEST(ActFloors, ActTwoFirstRoomIsFloorEighteen) {
    RunController rc = at_act1_boss_chest();
    cross_act(rc);
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    ASSERT_EQ(rc.run.floor, 17) << "the crossing itself adds no floor";
    EXPECT_EQ(run_cur_row(rc), -1) << "below row 0: the free first-row pick";

    const RunActionMask m = mask_of(rc);
    int col = -1;
    for (int x = 0; x < kMapCols; ++x) {
        if (m.can_choose_node[x]) { col = x; break; }
    }
    ASSERT_GE(col, 0) << "the new act's map must offer a row-0 start";
    step(rc, make_action(ActionVerb::CHOOSE, static_cast<uint8_t>(col)));

    EXPECT_EQ(rc.run.floor, 18) << "Act 2's first playable room";
    EXPECT_EQ(run_cur_row(rc), 0);
}

TEST(ActFloors, ActThreeFirstRoomIsFloorThirtyFive) {
    RunController rc = at_act1_boss_chest();
    cross_act(rc);
    place_at_boss_chest(rc);
    ASSERT_EQ(rc.run.floor, 34) << "the Act-2 chest";
    cross_act(rc);
    ASSERT_EQ(rc.run.act, 3);
    ASSERT_EQ(rc.run.floor, 34);
    EXPECT_EQ(run_cur_row(rc), -1);

    const RunActionMask m = mask_of(rc);
    int col = -1;
    for (int x = 0; x < kMapCols; ++x) {
        if (m.can_choose_node[x]) { col = x; break; }
    }
    ASSERT_GE(col, 0);
    step(rc, make_action(ActionVerb::CHOOSE, static_cast<uint8_t>(col)));
    EXPECT_EQ(rc.run.floor, 35) << "Act 3's first playable room";
    EXPECT_EQ(run_cur_row(rc), 0);
}

TEST(RunCurRow, RowIsFloorMinusActBaseMinusOne) {
    RunController rc{};
    for (int act = 1; act <= 3; ++act) {
        rc.run.act = static_cast<uint8_t>(act);
        const int base = act_floor_base(act);
        rc.run.floor = static_cast<uint16_t>(base);
        EXPECT_EQ(run_cur_row(rc), -1) << "act " << act << " base floor";
        for (int row = 0; row < kMapRows; ++row) {
            rc.run.floor = static_cast<uint16_t>(base + row + 1);
            EXPECT_EQ(run_cur_row(rc), row) << "act " << act << " row " << row;
        }
    }
}

// =============================================================================
// Map generation for Acts 2-3 (design §4.3)
// =============================================================================

TEST(MapRng, ActOffsetsAreSeedPlusOneTwoHundredSixHundred) {
    // Exordium.java:56 (seed + actNum), TheCity.java:46 (seed + actNum*100),
    // TheBeyond.java:44 (seed + actNum*200) -- so +1, +200, +600.
    RngStream a1 = map_stream(kSeed, 1), e1 = from_seed(kSeed + 1);
    EXPECT_EQ(std::memcmp(&a1, &e1, sizeof a1), 0);
    RngStream a2 = map_stream(kSeed, 2), e2 = from_seed(kSeed + 200);
    EXPECT_EQ(std::memcmp(&a2, &e2, sizeof a2), 0);
    RngStream a3 = map_stream(kSeed, 3), e3 = from_seed(kSeed + 600);
    EXPECT_EQ(std::memcmp(&a3, &e3, sizeof a3), 0);
}

TEST(MapRooms, EmeraldDrawFiresOncePerGeneratedAct) {
    // setEmeraldElite (AbstractDungeon.java:539, 542-556) runs for EVERY
    // generated act on the fully-unlocked profile, spending one
    // mapRng.random(0, eliteNodes-1) (:551) -- per act now, not once per run.
    for (int act = 1; act <= 3; ++act) {
        const GeneratedMap g = generate_map(kSeed, act);
        const RoomAssignment ra = assign_room_types(g, kA20);
        ASSERT_GE(ra.elite_node_count, 1) << "act " << act;
        EXPECT_EQ(ra.rng.counter, g.rng.counter + 1)
            << "act " << act << ": exactly one draw past end-of-path-generation";
        EXPECT_GE(ra.emerald_x, 0) << "act " << act;
        EXPECT_GE(ra.emerald_y, 0) << "act " << act;
    }
}

TEST(ActTransition, TheActMapIsRegeneratedFromTheActMapRng) {
    RunController rc = at_act1_boss_chest();
    cross_act(rc);

    const GeneratedMap g = generate_map(kSeed, 2);
    const RoomAssignment ra = assign_room_types(g, kA20);
    RunState expect_rs{};
    expect_rs.run_seed = kSeed;
    encode_paths_into_run_state(g, expect_rs);
    encode_rooms_into_run_state(ra, expect_rs);

    EXPECT_EQ(std::memcmp(rc.run.map, expect_rs.map, sizeof expect_rs.map), 0)
        << "the Act-2 map must be the act-2 mapRng's map, in place";
    EXPECT_EQ(std::memcmp(&rc.run.map_rng, &expect_rs.map_rng,
                          sizeof expect_rs.map_rng), 0)
        << "and the stored mapRng must be the end-of-generateMap state";
    EXPECT_EQ(rc.emerald_x, static_cast<uint8_t>(ra.emerald_x));
    EXPECT_EQ(rc.emerald_y, static_cast<uint8_t>(ra.emerald_y));
}

TEST(ActTransition, TheCityInstallsAnEmptyCurrMapNodeAndTheBeyondDoesNot) {
    // TheCity.java:49-50 sets currMapNode = new MapRoomNode(0, -1) with an
    // EmptyRoom; TheBeyond.java:35-47 has no such lines, so Act 3 opens still
    // holding Act 2's off-grid TreasureRoomBoss node. Inert -- that room is
    // COMPLETE and nothing reads it before the first-row pick -- but real, and
    // pinned as the negative it is.
    RunController rc = at_act1_boss_chest();
    cross_act(rc);
    EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::None));
    EXPECT_EQ(rc.cur_x, 0);

    place_at_boss_chest(rc);
    cross_act(rc);
    ASSERT_EQ(rc.run.act, 3);
    EXPECT_EQ(rc.room_type, static_cast<uint8_t>(RoomType::TreasureBoss))
        << "TheBeyond never replaces currMapNode";
}

TEST(ActTransition, LeavingTheCarriedOverNodeNeverPopsMonsterList) {
    // The only thing that asymmetry could break: nextRoomTransition's
    // `instanceof MonsterRoom` arm (:1700-1707). Neither an EmptyRoom nor a
    // TreasureRoomBoss is one.
    RunController rc = at_act1_boss_chest();
    cross_act(rc);
    place_at_boss_chest(rc);
    cross_act(rc);
    ASSERT_EQ(rc.run.act, 3);
    ASSERT_EQ(rc.monster_cursor, 0);

    const RunActionMask m = mask_of(rc);
    for (int x = 0; x < kMapCols; ++x) {
        if (m.can_choose_node[x]) {
            step(rc, make_action(ActionVerb::CHOOSE, static_cast<uint8_t>(x)));
            break;
        }
    }
    EXPECT_EQ(rc.monster_cursor, 0)
        << "the act's first transition leaves an off-map non-monster room";
}

// =============================================================================
// Per-act list generation (design §2.5; traps 4 and 8)
// =============================================================================

TEST(Encounters, ActsTwoAndThreeDrawTwoWeakNotThree) {
    // Exordium.generateMonsters calls generateWeakEnemies(3) (:110-112);
    // TheCity (:88-92) and TheBeyond (:85-89) call generateWeakEnemies(2).
    EXPECT_EQ(weak_segment_for_act(1), 3);
    EXPECT_EQ(weak_segment_for_act(2), 2);
    EXPECT_EQ(weak_segment_for_act(3), 2);

    for (int act = 2; act <= 3; ++act) {
        RngStream mrng = from_seed(kSeed);
        MonsterLists lists{};
        generate_monster_lists(act, mrng, lists);
        const auto* weak = sts::registry::encounter_pool_table(
            act, sts::registry::EncounterPool::WEAK);
        ASSERT_NE(weak, nullptr);
        int weak_prefix = 0;
        for (std::size_t i = 0; i < 2u; ++i) {
            bool found = false;
            for (std::size_t j = 0; j < weak->count; ++j) {
                found = found || lists.monster_list[i] == weak->keys[j];
            }
            weak_prefix += found ? 1 : 0;
        }
        EXPECT_EQ(weak_prefix, 2) << "act " << act
                                  << ": entries 0-1 come from the WEAK pool";
    }
}

TEST(Encounters, MonsterListLengthIsFifteenInActsTwoAndThree) {
    EXPECT_EQ(monster_list_len_for_act(1), 16);
    EXPECT_EQ(monster_list_len_for_act(2), 15);
    EXPECT_EQ(monster_list_len_for_act(3), 15);
    for (int act = 1; act <= 3; ++act) {
        RngStream mrng = from_seed(kSeed);
        MonsterLists lists{};
        generate_monster_lists(act, mrng, lists);
        EXPECT_EQ(lists.monster_list_count, monster_list_len_for_act(act))
            << "act " << act;
        EXPECT_EQ(lists.elite_list_count, kEliteSegment) << "act " << act;
        EXPECT_EQ(lists.boss_list_count, 3) << "act " << act;
    }
}

TEST(Encounters, BossShuffleConsumesExactlyOneRandomLongPerAct) {
    // initializeBoss's fully-unlocked branch (TheCity.java:154-182 /
    // TheBeyond.java:148-176): three bossList.add in a FIXED order, then
    // Collections.shuffle(bossList, new Random(monsterRng.randomLong())). The
    // adds cost nothing; the shuffle costs exactly ONE randomLong, and it is the
    // LAST draw of the whole generateMonsters + initializeBoss sequence.
    //
    // That last claim is what makes this checkable from outside: under the
    // one-draw invariant the engine state after k wrapper draws depends only on
    // k, so a fresh stream advanced by (N-1) draws of ANY kind is byte-identical
    // to the real stream just before the shuffle -- and the randomLong taken
    // there must reproduce the observed boss order exactly.
    for (int act = 1; act <= 3; ++act) {
        RngStream mrng = from_seed(kSeed);
        MonsterLists lists{};
        generate_monster_lists(act, mrng, lists);
        const int32_t total = mrng.counter;
        ASSERT_EQ(lists.boss_list_count, 3) << "act " << act;

        RngStream replay = from_seed(kSeed);
        for (int32_t i = 0; i < total - 1; ++i) (void)random_boolean(replay);
        const int64_t shuffle_seed = random_long(replay);
        EXPECT_EQ(replay.counter, total) << "act " << act;
        EXPECT_EQ(replay.s0, mrng.s0)
            << "act " << act << ": the sequence must END on the randomLong";
        EXPECT_EQ(replay.s1, mrng.s1) << "act " << act;

        // The pre-shuffle list is the registry's id order, which is the game's
        // ArrayList add() order.
        const auto* bosses = sts::registry::encounter_pool_table(
            act, sts::registry::EncounterPool::BOSS);
        ASSERT_NE(bosses, nullptr);
        ASSERT_EQ(bosses->count, 3u);
        std::string_view expect[3] = {bosses->keys[0], bosses->keys[1],
                                      bosses->keys[2]};
        JdkRandom jr(shuffle_seed);
        jdk_shuffle(std::span<std::string_view>(expect, 3), jr);
        for (std::size_t i = 0; i < 3u; ++i) {
            EXPECT_EQ(lists.boss_list[i], expect[i])
                << "act " << act << " boss " << i;
        }
    }
}

TEST(Encounters, ThreeDarklingsSelfExclusionFires) {
    // TheBeyond.generateExclusions (:131-134): "3 Darklings" is in BOTH pools
    // and its weak row excludes its own key (trap 8). The registry keeps two
    // rows with one game_id, and encounter_by_game_id returns the lower-id
    // (weak) one -- which is the one carrying the exclusion.
    const auto* weak_row = sts::registry::encounter_by_game_id("3 Darklings");
    ASSERT_NE(weak_row, nullptr);
    EXPECT_EQ(weak_row->act, 3);
    EXPECT_EQ(weak_row->pool, sts::registry::EncounterPool::WEAK);
    ASSERT_EQ(weak_row->exclude_count, 1);
    EXPECT_EQ(weak_row->excludes[0], std::string_view{"3 Darklings"});

    // And when the second weak draw IS "3 Darklings", the first-strong slot can
    // never be it. Scan seeds until that prefix occurs so the assertion is about
    // a state the generator really produces.
    bool observed = false;
    for (int64_t seed = 0; seed < 400 && !observed; ++seed) {
        RngStream mrng = from_seed(seed);
        MonsterLists lists{};
        generate_monster_lists(3, mrng, lists);
        if (lists.monster_list[1] != std::string_view{"3 Darklings"}) continue;
        observed = true;
        EXPECT_NE(lists.monster_list[2], std::string_view{"3 Darklings"})
            << "seed " << seed << ": the first-strong roll must reject its own "
               "key while the exclusion is live";
    }
    EXPECT_TRUE(observed) << "no seed in the scan produced the prefix -- the "
                             "exclusion would then be untested, not satisfied";
}

TEST(Encounters, FirstStrongRerollConsumesOneMonsterRngPerRejection) {
    // populateFirstStrongEnemy (AbstractDungeon.java:1057-1062) rolls until the
    // result is not excluded, and every REJECTED roll has already spent one
    // monsterRng.random(). The draw count is part of the contract, so it is
    // measured rather than assumed: the total draw count must exceed the
    // rejection-free minimum whenever a rejection is reachable.
    //
    // Minimum for an act: weak + 1 first-strong + 12 strong + 10 elites + 1
    // randomLong, with every roll accepted.
    for (int act = 2; act <= 3; ++act) {
        const int32_t floor_draws =
            weak_segment_for_act(act) + 1 + kStrongSegment + kEliteSegment + 1;
        bool saw_extra = false;
        for (int64_t seed = 0; seed < 200 && !saw_extra; ++seed) {
            RngStream mrng = from_seed(seed);
            MonsterLists lists{};
            generate_monster_lists(act, mrng, lists);
            EXPECT_GE(mrng.counter, floor_draws) << "act " << act;
            saw_extra = mrng.counter > floor_draws;
        }
        EXPECT_TRUE(saw_extra)
            << "act " << act
            << ": no seed in the scan rejected a roll -- the rejection loop "
               "would then be untested";
    }
}

TEST(ActTransition, ListsAreRegeneratedOffTheContinuingMonsterRng) {
    // dungeonTransitionSetup clears the three lists (:2576-2580) and
    // generateMonsters/initializeBoss rebuild them off the RUN-LIFETIME
    // monsterRng, which nothing at the boundary reseeds. So the Act-2 lists are
    // exactly what the Act-1 stream position produces next.
    RunController rc = at_act1_boss_chest();
    RngStream expected = rc.run.monster_rng;
    MonsterLists expect_lists{};
    generate_monster_lists(2, expected, expect_lists);

    cross_act(rc);

    EXPECT_EQ(rc.run.monster_rng.counter, expected.counter);
    EXPECT_EQ(rc.run.monster_rng.s0, expected.s0);
    EXPECT_EQ(rc.lists.monster_list_count, expect_lists.monster_list_count);
    for (uint8_t i = 0; i < expect_lists.monster_list_count; ++i) {
        EXPECT_EQ(rc.lists.monster_list[i], expect_lists.monster_list[i])
            << "entry " << static_cast<int>(i);
    }
    for (uint8_t i = 0; i < expect_lists.boss_list_count; ++i) {
        EXPECT_EQ(rc.lists.boss_list[i], expect_lists.boss_list[i]);
    }
    EXPECT_EQ(rc.monster_cursor, 0);
    EXPECT_EQ(rc.elite_cursor, 0);
    EXPECT_EQ(rc.boss_cursor, 0);
}

TEST(ActTransition, TheActBossIsMirroredIntoSaveParityState) {
    RunController rc = at_act1_boss_chest();
    const uint16_t act1_boss = rc.run.boss_ids[0];
    ASSERT_NE(act1_boss, 0);
    cross_act(rc);
    EXPECT_EQ(rc.run.boss_ids[0], act1_boss) << "Act 1's stays put";
    ASSERT_NE(rc.run.boss_ids[1], 0);
    const auto* def = sts::registry::encounter_by_game_id(rc.lists.boss_list[0]);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(rc.run.boss_ids[1], static_cast<uint16_t>(def->id));
}

// =============================================================================
// Per-act constants (design §2.5; trap 2)
// =============================================================================

TEST(CardUpgradedChance, IsActAndAscensionKeyed) {
    // Exordium.java:107 / TheCity.java:84 / TheBeyond.java:81.
    EXPECT_FLOAT_EQ(card_upgraded_chance(1, 0), 0.0f);
    EXPECT_FLOAT_EQ(card_upgraded_chance(1, 20), 0.0f);
    EXPECT_FLOAT_EQ(card_upgraded_chance(2, 11), 0.25f);
    EXPECT_FLOAT_EQ(card_upgraded_chance(2, 12), 0.125f);
    EXPECT_FLOAT_EQ(card_upgraded_chance(2, 20), 0.125f);
    EXPECT_FLOAT_EQ(card_upgraded_chance(3, 11), 0.5f);
    EXPECT_FLOAT_EQ(card_upgraded_chance(3, 12), 0.25f);
    EXPECT_FLOAT_EQ(card_upgraded_chance(3, 20), 0.25f);
}

// =============================================================================
// The three-act sim (the ledger's Acceptance)
// =============================================================================
//
// HONEST SCOPE. Act-2/3 MONSTERS are S2.2x and the per-act event lists are
// S2.13, so a run driven purely by map choices cannot walk INTO an Act-2 room --
// the first row of every act is forced Monster (map_rooms.hpp) and an
// unimplemented encounter parks at ROOM_UNIMPLEMENTED by design. The driver
// below therefore does two things and keeps them separate:
//
//   * it steps every state the run layer CAN produce, through the real
//     advance() / legal_actions() API under a fixed, RNG-free policy;
//   * where content is missing it advances the run the way the boss reward would
//     -- the same public transition edge place_at_boss_chest uses -- rather than
//     writing state by hand.
//
// The determinism claim is over the WHOLE chain including the parked rooms, and
// the run demonstrably reaches Act 3 and floor 35. When S2.2x lands, the
// placement can be deleted and the same driver becomes a pure policy walk; the
// residue is recorded in the ledger Log against that dependency.

// A deterministic, RNG-free policy: always take the lowest legal column, always
// proceed when nothing else is offered. Records a hash after every step.
void drive_three_acts(int64_t seed, uint8_t ascension,
                      std::vector<uint64_t>& chain, RunController& out) {
    RunController rc = run_begin(seed, ascension);
    chain.push_back(hash_controller_bytes(rc));

    rc.neow.screen = static_cast<uint8_t>(NeowScreen::DONE);
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    chain.push_back(hash_controller_bytes(rc));

    for (int act = 1; act <= 3; ++act) {
        // Walk the act's map for as long as the run layer keeps offering moves.
        for (int guard = 0; guard < 64; ++guard) {
            const RunActionMask m = mask_of(rc);
            if (static_cast<RunPhase>(rc.phase) != RunPhase::MAP_CHOICE) break;
            int col = -1;
            for (int x = 0; x < kMapCols; ++x) {
                if (m.can_choose_node[x]) { col = x; break; }
            }
            if (col < 0) {
                if (!m.can_choose_boss) break;
                step(rc, make_action(ActionVerb::CHOOSE, kChooseBoss));
            } else {
                step(rc, make_action(ActionVerb::CHOOSE,
                                     static_cast<uint8_t>(col)));
            }
            chain.push_back(hash_controller_bytes(rc));
        }
        if (act == 3) break;
        // Content for this act's rooms is not landed, so advance the run over
        // the boss the way the reward screen's proceed does, then cross.
        place_at_boss_chest(rc);
        chain.push_back(hash_controller_bytes(rc));
        // Exercise the chest's own screens so the crossing is reached from the
        // real flow, not from a hand-set phase.
        for (int guard = 0; guard < 24; ++guard) {
            const RunActionMask m = mask_of(rc);
            if (static_cast<RunPhase>(rc.phase) != RunPhase::BOSS_TREASURE) break;
            // Priority: finish any modal grid the picked relic opened, then
            // claim, then open, then proceed. Every branch is a real CHOOSE
            // through the mask, so the chest's own screens are exercised.
            bool acted = false;
            for (uint16_t i = 0; i < kMasterDeckCap && !acted; ++i) {
                if (m.can_choose_master_deck[i]) {
                    step(rc, make_action(ActionVerb::CHOOSE,
                                         static_cast<uint8_t>(i)));
                    acted = true;
                }
            }
            for (int i = 0; i < kRewardItemCap && !acted; ++i) {
                if (m.can_claim_reward[i]) {
                    step(rc, make_action(ActionVerb::CHOOSE,
                                         static_cast<uint8_t>(i)));
                    acted = true;
                }
            }
            if (!acted && m.can_open_chest) {
                step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
                acted = true;
            }
            if (!acted) {
                ASSERT_TRUE(m.can_proceed)
                    << "the boss chest offered nothing at all";
                step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
            }
            chain.push_back(hash_controller_bytes(rc));
        }
    }
    out = rc;
}

TEST(ThreeActSim, ScriptedPolicyRunCompletesDeterministicallyTwiceWithIdenticalHashes) {
    std::vector<uint64_t> a, b;
    RunController end_a{}, end_b{};
    drive_three_acts(kSeed, kA20, a, end_a);
    drive_three_acts(kSeed, kA20, b, end_b);

    ASSERT_FALSE(a.empty());
    ASSERT_EQ(a.size(), b.size()) << "the two runs took different step counts";
    for (std::size_t i = 0; i < a.size(); ++i) {
        ASSERT_EQ(a[i], b[i]) << "the hash chains diverge at step " << i;
    }
    EXPECT_EQ(hash_controller_bytes(end_a), hash_controller_bytes(end_b));

    // And it really went the distance: Act 3, past its first room.
    EXPECT_EQ(end_a.run.act, 3);
    EXPECT_GE(end_a.run.floor, 35)
        << "the driver must reach Act 3's first playable room, not merely "
           "construct the act";
}

TEST(ThreeActSim, DifferentSeedsProduceDifferentChains) {
    // The negative control: an equality test over two identical runs passes
    // vacuously if the hash is constant.
    std::vector<uint64_t> a, b;
    RunController end_a{}, end_b{};
    drive_three_acts(kSeed, kA20, a, end_a);
    drive_three_acts(kSeed + 1, kA20, b, end_b);
    EXPECT_NE(hash_controller_bytes(end_a), hash_controller_bytes(end_b));
}

// =============================================================================
// The victory terminal, moved to the Act-3 boss
// =============================================================================

TEST(RunTerminal, TheActOneChestProceedIsNoLongerTerminal) {
    RunController rc = at_act1_boss_chest();
    const StepResult res =
        step_with_result(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_FALSE(res.terminal);
    EXPECT_EQ(res.reward, 0.0f);
    EXPECT_FALSE(run_is_victory(rc));
    EXPECT_EQ(rc.run.boss_chest.relics[0], 0) << "the room's state was cleared";
}

TEST(RunTerminal, VictoryIsTheActThreeBossAndOnlyThat) {
    // run_is_victory reads (RUN_OVER, act 3, Boss, KILLED). Each conjunct is
    // probed so none of them is decorative.
    RunController rc{};
    rc.phase = static_cast<uint8_t>(RunPhase::RUN_OVER);
    rc.run.act = 3;
    rc.room_type = static_cast<uint8_t>(RoomType::Boss);
    rc.combat_outcome = static_cast<uint8_t>(RunCombatOutcome::KILLED);
    EXPECT_TRUE(run_is_victory(rc));

    RunController died = rc;
    died.combat_outcome = static_cast<uint8_t>(RunCombatOutcome::DEFEAT);
    EXPECT_FALSE(run_is_victory(died)) << "a death in the same room is not a win";

    RunController act2 = rc;
    act2.run.act = 2;
    EXPECT_FALSE(run_is_victory(act2)) << "the Act-2 boss is not the terminal";

    RunController chest = rc;
    chest.room_type = static_cast<uint8_t>(RoomType::TreasureBoss);
    EXPECT_FALSE(run_is_victory(chest))
        << "the boss chest stopped being the terminal at S2.12";

    RunController live = rc;
    live.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
    EXPECT_FALSE(run_is_victory(live));
}

}  // namespace
}  // namespace sts::engine
