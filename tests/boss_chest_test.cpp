// S2.11 -- the boss chest (TreasureRoomBoss) and the boss-relic pick.
//
// The properties under test, in the order the ledger's Acceptance names them:
//   * POP ORDER + TIMING: three FRONT pops of the BOSS pool, at ROOM ENTRY, in
//     pool order, consuming NO rng stream (trap 3).
//   * POOL DEPLETION ACROSS BOTH CONSUMERS: the Neow boss swap and this chest
//     share one pool cursor, and skipped relics never come back.
//   * SKIP: a reversible screen close that burns nothing and reopens with the
//     same three; proceeding without ever opening still burned three.
//   * canSpawn / trap 9: Ectoplasm accepted at the Act-1 chest and rejected
//     (and CONSUMED) at an Act-2 one; Black Blood dead forever after the swap;
//     an exhausted pool offers three Circlets.
//   * THE SAPPHIRE-KEY PIN: the boss chest's open path can never append one.
//   * THE TERMINAL: the boss reward's proceed enters the chest (a full room
//     transition), and the CHEST's proceed is the act terminal seam.
//
// Provenance is in boss_chest.hpp's header block; citations are repeated here
// only where a test pins one specific line.

#include "sts/engine/boss_chest.hpp"

#include <cstring>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "sts/engine/neow.hpp"
#include "sts/engine/public_view.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/twin.hpp"

namespace sts::engine {
namespace {

constexpr int64_t kSeed = 12345;
constexpr uint8_t kA20 = 20;
constexpr int kBossPool = static_cast<int>(RelicPool::BOSS);

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

// A run parked in the boss chest, WITHOUT walking a whole act: the chest room is
// reached by the public transition helper, which is exactly the edge the boss
// reward's proceed takes.
//
// The floor is placed on the Act-1 BOSS floor first (S2.12). It used to be left
// at 1, on the grounds that nothing in this room reads the floor except the
// reseed -- true while the chest's proceed was the run's end, and false the
// moment it became an act transition, because run_cur_row() is now a function of
// BOTH floor and act and an (act 2, floor 2) controller is not a state the game
// can be in.
RunController at_boss_chest(int64_t seed = kSeed, uint8_t ascension = kA20) {
    RunController rc = run_begin(seed, ascension);
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::DONE);
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    // Pretend the boss room was just left, which is what the chest transition
    // always follows.
    rc.run.floor = static_cast<uint16_t>(kActFloorSpan - 1);  // the Act-1 boss
    rc.room_type = static_cast<uint8_t>(RoomType::Boss);
    rc.cur_x = static_cast<uint8_t>(kBossCol);
    rc.combat_outcome = static_cast<uint8_t>(RunCombatOutcome::KILLED);
    next_room_transition_boss_chest(rc);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::BOSS_TREASURE));
    EXPECT_EQ(rc.run.floor, kActFloorSpan);  // 17 -- see act_floor_base
    return rc;
}

std::vector<uint16_t> boss_pool(const RunState& rs) {
    return std::vector<uint16_t>(
        rs.relic_pools[kBossPool],
        rs.relic_pools[kBossPool] + rs.relic_pool_count[kBossPool]);
}

RunActionMask mask_of(const RunController& rc) {
    RunActionMask m{};
    legal_actions(rc, m);
    return m;
}

bool owns(const RunState& rs, RelicId id) {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id == static_cast<uint16_t>(id)) return true;
    }
    return false;
}

// =============================================================================
// Pop order, timing, and the RNG-free contract (trap 3)
// =============================================================================

TEST(BossChest, PopsThreeFromTheFrontOfTheBossPoolAtRoomEntry) {
    RunController rc = run_begin(kSeed, kA20);
    const std::vector<uint16_t> before = boss_pool(rc.run);
    ASSERT_GE(before.size(), 6u) << "the live boss pool must be deep enough";

    RunState rs = rc.run;
    const BossChestState chest = roll_boss_chest(rs);

    // BossChest.java:37-38 adds three returnRandomRelic(BOSS) in a plain loop,
    // and the BOSS arm of returnRandomRelicKey is remove(0)
    // (AbstractDungeon.java:792-798): the offers ARE the pool's first three, in
    // order, on a run where no gate rejects.
    EXPECT_EQ(chest.relics[0], before[0]);
    EXPECT_EQ(chest.relics[1], before[1]);
    EXPECT_EQ(chest.relics[2], before[2]);
    EXPECT_EQ(rs.relic_pool_count[kBossPool], before.size() - 3);
    const std::vector<uint16_t> after = boss_pool(rs);
    EXPECT_TRUE(std::equal(after.begin(), after.end(), before.begin() + 3))
        << "the remainder must be the untouched tail -- no reordering";
}

TEST(BossChest, PopsHappenAtEntryNotAtOpen) {
    RunController rc = at_boss_chest();
    const uint8_t after_entry = rc.run.relic_pool_count[kBossPool];
    const BossChestState offered = rc.run.boss_chest;
    ASSERT_EQ(rc.run.boss_chest.screen,
              static_cast<uint8_t>(BossChestScreen::CLOSED));
    ASSERT_EQ(rc.run.boss_chest.seen, 0);

    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    ASSERT_EQ(rc.run.boss_chest.screen,
              static_cast<uint8_t>(BossChestScreen::RELIC_SELECT));

    EXPECT_EQ(rc.run.relic_pool_count[kBossPool], after_entry)
        << "opening the chest pops nothing -- BossChest's constructor already "
           "did (TreasureRoomBoss.java:63)";
    EXPECT_EQ(std::memcmp(rc.run.boss_chest.relics, offered.relics,
                          sizeof offered.relics),
              0);
}

TEST(BossChest, ConsumesNoRngStreamAtEntryOrOpen) {
    RunController rc = run_begin(kSeed, kA20);
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::DONE);
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    rc.room_type = static_cast<uint8_t>(RoomType::Boss);
    rc.cur_x = static_cast<uint8_t>(kBossCol);

    const RunState before = rc.run;
    next_room_transition_boss_chest(rc);

    // Every RUN-scoped stream: the pops read no randomness at all
    // (AbstractDungeon.java:792-798 and the :739-745 reroute are both plain
    // remove(0)). The FLOOR-scoped five are excluded because the transition
    // legitimately reseeds them (trap 7), which is a different test below.
    EXPECT_EQ(rc.run.relic_rng.counter, before.relic_rng.counter);
    EXPECT_EQ(rc.run.treasure_rng.counter, before.treasure_rng.counter);
    EXPECT_EQ(rc.run.card_rng.counter, before.card_rng.counter);
    EXPECT_EQ(rc.run.merchant_rng.counter, before.merchant_rng.counter);
    EXPECT_EQ(rc.run.potion_rng.counter, before.potion_rng.counter);
    EXPECT_EQ(rc.run.event_rng.counter, before.event_rng.counter);
    EXPECT_EQ(rc.run.monster_rng.counter, before.monster_rng.counter);

    const RunState at_entry = rc.run;
    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    EXPECT_EQ(rc.run.relic_rng.counter, at_entry.relic_rng.counter)
        << "no chest relic hook fires on the boss path, so relicRng is "
           "untouched (Matryoshka is skipped at BossChest.java:53 and "
           "CursedKey guards !bossChest)";
    EXPECT_EQ(rc.run.treasure_rng.counter, at_entry.treasure_rng.counter)
        << "BossChest.open fully overrides AbstractChest.open, so "
           "randomizeReward's treasureRng d100 never runs";
}

TEST(BossChest, EntryIsAFullRoomTransitionWithTheTrapSevenReseed) {
    RunController rc = run_begin(kSeed, kA20);
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::DONE);
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    rc.room_type = static_cast<uint8_t>(RoomType::Boss);
    rc.cur_x = static_cast<uint8_t>(kBossCol);
    const uint16_t floor_before = rc.run.floor;

    next_room_transition_boss_chest(rc);

    // goToTreasureRoom -> nextRoomTransitionStart -> updateFading's
    // `if (!isDungeonBeaten) nextRoomTransition()` (AbstractDungeon.java:
    // 2317-2325). The scout dossier claimed this room was "off-map and
    // floor-less"; it is off-map, and it is NOT floor-less.
    EXPECT_EQ(rc.run.floor, floor_before + 1);
    const RngStream expect = floor_stream(rc.run.run_seed,
                                          static_cast<int32_t>(rc.run.floor));
    EXPECT_EQ(rc.combat.misc_rng.s0, expect.s0);
    EXPECT_EQ(rc.combat.misc_rng.s1, expect.s1);
    EXPECT_EQ(rc.combat.misc_rng.counter, expect.counter);
    EXPECT_EQ(rc.combat.ai_rng.s0, expect.s0);
    EXPECT_EQ(rc.combat.card_random_rng.s0, expect.s0);
}

TEST(BossChest, MawBankPaysItsTwelveGoldOnEntry) {
    RunController rc = run_begin(kSeed, kA20);
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::DONE);
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    ASSERT_EQ(acquire_relic(rc.run, rc.combat.misc_rng, RelicId::MAW_BANK),
              RelicAcquireResult::ACQUIRED);
    rc.room_type = static_cast<uint8_t>(RoomType::Boss);
    rc.cur_x = static_cast<uint8_t>(kBossCol);
    const int32_t gold_before = rc.run.gold;

    next_room_transition_boss_chest(rc);

    // MawBank.onEnterRoom (MawBank.java:29-35) has no room-kind gate at all,
    // and the boss chest IS a room entry -- this is the observable half of the
    // dossier correction.
    EXPECT_EQ(rc.run.gold, gold_before + 12);
}

TEST(BossChest, LeavingTheBossRoomConsumesAMonsterListEntry) {
    RunController rc = run_begin(kSeed, kA20);
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::DONE);
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    rc.room_type = static_cast<uint8_t>(RoomType::Boss);
    rc.cur_x = static_cast<uint8_t>(kBossCol);
    rc.run.floor = 16;
    const uint8_t cursor_before = rc.monster_cursor;

    next_room_transition_boss_chest(rc);

    // `MonsterRoomBoss extends MonsterRoom` (MonsterRoomBoss.java:18-19), so
    // nextRoomTransition's `instanceof MonsterRoom` arm (:1700-1707) fires when
    // the boss room is left. Unobservable in S1 (nothing read the list again);
    // this edge is the first that leaves a boss room with the run still going.
    EXPECT_EQ(rc.monster_cursor, cursor_before + 1);
}

// =============================================================================
// Depletion across BOTH consumers, and the permanence of a skip
// =============================================================================

TEST(BossChest, NeowBossSwapThenBossChestShareOnePoolCursor) {
    RunController rc = run_begin(kSeed, kA20);
    const std::vector<uint16_t> initial = boss_pool(rc.run);

    // Find and take the BOSS_RELIC blessing, which front-pops the same pool
    // (NeowReward.activate case 13, :243-247 -- loseRelic THEN the draw).
    int option = -1;
    for (int i = 0; i < kNeowOptionCount; ++i) {
        if (rc.neow.option_type[i] ==
            static_cast<uint8_t>(NeowRewardType::BOSS_RELIC)) {
            option = i;
        }
    }
    ASSERT_GE(option, 0) << "seed " << kSeed << " must offer the boss swap";
    step(rc, make_action(ActionVerb::CHOOSE, static_cast<uint8_t>(option)));
    const uint8_t after_swap = rc.run.relic_pool_count[kBossPool];
    ASSERT_LT(after_swap, initial.size()) << "the swap must have popped";

    // Resolve whatever screen the swapped relic opened, then leave Neow.
    for (int guard = 0; guard < 40 &&
                        rc.phase == static_cast<uint8_t>(RunPhase::NEOW) &&
                        rc.neow.screen != static_cast<uint8_t>(NeowScreen::DONE);
         ++guard) {
        const RunActionMask m = mask_of(rc);
        bool acted = false;
        for (uint16_t i = 0; i < kMasterDeckCap && !acted; ++i) {
            if (m.can_choose_master_deck[i]) {
                step(rc, make_action(ActionVerb::CHOOSE,
                                     static_cast<uint8_t>(i)));
                acted = true;
            }
        }
        if (!acted && m.can_proceed) {
            step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
            acted = true;
        }
        if (!acted) break;
    }
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::DONE);
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    rc.room_type = static_cast<uint8_t>(RoomType::Boss);
    rc.cur_x = static_cast<uint8_t>(kBossCol);
    const uint8_t before_chest = rc.run.relic_pool_count[kBossPool];

    next_room_transition_boss_chest(rc);

    // The chest continues where the swap left off: the offers come from the
    // post-swap front, never from the original pool head.
    EXPECT_LE(rc.run.relic_pool_count[kBossPool], before_chest - 3)
        << "three more pops, at least (a canSpawn rejection consumes extra)";
    for (int i = 0; i < kBossChestOfferCount; ++i) {
        EXPECT_NE(rc.run.boss_chest.relics[i], initial[0])
            << "offer " << i << " re-served the relic the Neow swap took";
    }
}

TEST(BossChest, SkippedRelicsAreNeverReturnedToThePool) {
    RunController rc = at_boss_chest();
    const std::vector<uint16_t> pool_at_entry = boss_pool(rc.run);
    const BossChestState offered = rc.run.boss_chest;

    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    step(rc, make_action(ActionVerb::CHOOSE, kChooseCancelGrid));  // skip
    ASSERT_EQ(rc.run.boss_chest.screen,
              static_cast<uint8_t>(BossChestScreen::CLOSED));

    EXPECT_EQ(boss_pool(rc.run), pool_at_entry)
        << "relicSkipLogic (BossRelicSelectScreen.java:202-212) touches no pool";
    for (uint16_t id : pool_at_entry) {
        for (int i = 0; i < kBossChestOfferCount; ++i) {
            EXPECT_NE(id, offered.relics[i])
                << "a burned relic reappeared in the pool";
        }
    }
}

TEST(BossChest, ProceedWithoutEverOpeningStillBurnedThreeRelics) {
    RunController rc = at_boss_chest();
    const uint8_t at_entry = rc.run.relic_pool_count[kBossPool];
    const uint8_t relics_before = rc.run.relic_count;

    // Straight past the chest -- the noPick path (ProceedButton.java:232-234).
    const StepResult res =
        step_with_result(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));

    EXPECT_EQ(rc.run.relic_pool_count[kBossPool], at_entry)
        << "the burn already happened at entry; leaving costs nothing MORE";
    EXPECT_EQ(rc.run.relic_count, relics_before) << "noPick grants nothing";
    // S2.12: the proceed is the ACT TRANSITION, not the run's end.
    EXPECT_FALSE(res.terminal);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(rc.run.act, 2);
}

TEST(BossChest, ProceedAfterSkipRecordsNoPickAndMutatesNothing) {
    RunController rc = at_boss_chest();
    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    step(rc, make_action(ActionVerb::CHOOSE, kChooseCancelGrid));
    const RunState before = rc.run;

    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));

    // noPick's whole body is a metrics append (BossRelicSelectScreen.java:
    // 240-248): no relic, no pool write, no deck write. Since S2.12 the same
    // press also runs the act transition, so a whole-RunState byte compare is
    // no longer the right instrument -- it would be asserting that the crossing
    // does nothing. What noPick itself owns is pinned exactly:
    EXPECT_EQ(rc.run.relic_count, before.relic_count);
    for (int t = 0; t < kRelicTierCount; ++t) {
        EXPECT_EQ(rc.run.relic_pool_count[t], before.relic_pool_count[t])
            << "pool tier " << t;
        EXPECT_EQ(std::memcmp(rc.run.relic_pools[t], before.relic_pools[t],
                              sizeof before.relic_pools[t]),
                  0)
            << "pool tier " << t;
    }
    EXPECT_EQ(rc.run.master_deck_count, before.master_deck_count);
    EXPECT_EQ(rc.run.gold, before.gold);
}

// =============================================================================
// Skip is a reversible screen close
// =============================================================================

TEST(BossChest, SkipClosesTheChestWithoutBurningAnything) {
    RunController rc = at_boss_chest();
    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    const RunState before = rc.run;

    RunActionMask m = mask_of(rc);
    ASSERT_TRUE(m.can_cancel_grid) << "the screen's cancel button IS the skip";
    ASSERT_FALSE(m.can_proceed)
        << "bossRelicScreen.open hides the proceed button (:354)";

    step(rc, make_action(ActionVerb::CHOOSE, kChooseCancelGrid));
    // The ONLY RunState byte a skip moves is the screen bit itself (schema v8
    // seated BossChestState inside RunState): chest.close() sets isOpen = false
    // and nothing else -- no pool write, no relic, no deck -- and `seen` stays,
    // because closing the chest cannot unsee it.
    EXPECT_EQ(rc.run.boss_chest.screen,
              static_cast<uint8_t>(BossChestScreen::CLOSED));
    EXPECT_EQ(rc.run.boss_chest.seen, 1);
    RunState after = rc.run;
    after.boss_chest.screen = before.boss_chest.screen;
    EXPECT_EQ(std::memcmp(&before, &after, sizeof before), 0)
        << "a skip may move no RunState byte besides the chest screen";
    EXPECT_EQ(rc.run.boss_chest.chose_relic, 0);
}

TEST(BossChest, ReopensAfterSkipWithTheSameThreeRelics) {
    RunController rc = at_boss_chest();
    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    const BossChestState offered = rc.run.boss_chest;
    step(rc, make_action(ActionVerb::CHOOSE, kChooseCancelGrid));

    RunActionMask m = mask_of(rc);
    EXPECT_TRUE(m.can_open_chest)
        << "TreasureRoomBoss.update keeps chest.update() live (:66-71) and "
           "close() only cleared isOpen -- the chest is clickable again";
    EXPECT_TRUE(m.can_proceed) << "and the room is COMPLETE, so leaving is too";

    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    EXPECT_EQ(rc.run.boss_chest.screen,
              static_cast<uint8_t>(BossChestScreen::RELIC_SELECT));
    EXPECT_EQ(std::memcmp(rc.run.boss_chest.relics, offered.relics,
                          sizeof offered.relics),
              0)
        << "BossChest.relics was never cleared, so open() re-offers the SAME "
           "three (BossRelicSelectScreen.java:342-373 rebuilds from that list)";
}

// =============================================================================
// The pick
// =============================================================================

TEST(BossChest, PickingTakesOneAndDropsTheOtherTwoForGood) {
    RunController rc = at_boss_chest();
    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    const BossChestState offered = rc.run.boss_chest;
    const std::vector<uint16_t> pool_before = boss_pool(rc.run);

    const RunActionMask m = mask_of(rc);
    ASSERT_TRUE(m.can_claim_reward[0]);
    ASSERT_TRUE(m.can_claim_reward[1]);
    ASSERT_TRUE(m.can_claim_reward[2]);
    ASSERT_FALSE(m.can_claim_reward[3]) << "exactly three rows, never more";

    step(rc, make_action(ActionVerb::CHOOSE, 1));

    EXPECT_EQ(rc.run.boss_chest.chose_relic, 1);
    EXPECT_TRUE(owns(rc.run, static_cast<RelicId>(offered.relics[1])));
    EXPECT_EQ(boss_pool(rc.run), pool_before)
        << "the two unpicked relics are dropped, not returned";
    for (int i : {0, 2}) {
        for (uint16_t id : pool_before) {
            EXPECT_NE(id, offered.relics[i]);
        }
    }
}

TEST(BossChest, PickingEndsTheRoomAtProceedOnly) {
    RunController rc = at_boss_chest();
    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    step(rc, make_action(ActionVerb::CHOOSE, 0));
    // A relic with no on_equip_screen body finishes synchronously; one with a
    // screen leaves an equip screen up. Either way the chest can never be
    // reopened, and once resolved the only move is Proceed.
    for (int guard = 0; guard < 20 &&
                        rc.run.boss_chest.screen !=
                            static_cast<uint8_t>(BossChestScreen::DONE);
         ++guard) {
        const RunActionMask m = mask_of(rc);
        bool acted = false;
        for (uint16_t i = 0; i < kMasterDeckCap && !acted; ++i) {
            if (m.can_choose_master_deck[i]) {
                step(rc, make_action(ActionVerb::CHOOSE,
                                     static_cast<uint8_t>(i)));
                acted = true;
            }
        }
        if (!acted && m.can_proceed) {
            step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
            acted = true;
        }
        ASSERT_TRUE(acted) << "an equip screen with no legal move";
        ASSERT_NE(rc.phase, static_cast<uint8_t>(RunPhase::RUN_OVER))
            << "an equip screen's proceed must not fall through to the "
               "terminal";
    }
    ASSERT_EQ(rc.run.boss_chest.screen,
              static_cast<uint8_t>(BossChestScreen::DONE));

    const RunActionMask m = mask_of(rc);
    EXPECT_TRUE(m.can_proceed);
    EXPECT_FALSE(m.can_open_chest) << "the chest cannot be reopened after a pick";
    EXPECT_FALSE(m.can_cancel_grid);
    for (int i = 0; i < kRewardItemCap; ++i) {
        EXPECT_FALSE(m.can_claim_reward[i]);
    }
}

// The re-homed on_equip_screen site (relic_pools.hpp:126-128). Astrolabe is
// forced into slot 0 so the grid it opens is exercised at the CHEST rather than
// only at Neow -- the pick has to run the body, not refuse it.
TEST(BossChest, PickingAnEquipScreenRelicOpensItsGridAtThisSite) {
    RunController rc = at_boss_chest();
    rc.run.boss_chest.relics[0] = static_cast<uint16_t>(RelicId::ASTROLABE);
    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    step(rc, make_action(ActionVerb::CHOOSE, 0));

    ASSERT_EQ(rc.run.boss_chest.screen,
              static_cast<uint8_t>(BossChestScreen::EQUIP_GRID))
        << "Astrolabe's onEquip asks for a transform+upgrade grid; the plain "
           "acquire door would have REFUSED it (NEEDS_EQUIP_CONTEXT), which is "
           "the silent-inert failure this site exists to prevent";
    EXPECT_TRUE(owns(rc.run, RelicId::ASTROLABE));
    EXPECT_EQ(rc.neow.grid_mode,
              static_cast<uint8_t>(NeowGridMode::TRANSFORM_UPGRADE));
    EXPECT_EQ(rc.neow.grid_needed, 3);

    const RunActionMask m = mask_of(rc);
    bool any_row = false;
    for (uint16_t i = 0; i < kMasterDeckCap; ++i) {
        any_row = any_row || m.can_choose_master_deck[i];
    }
    EXPECT_TRUE(any_row) << "the grid must offer its rows";
    EXPECT_FALSE(m.can_proceed) << "a picking grid is modal, not skippable";

    // Complete the grid; the room then finishes.
    for (int pick = 0; pick < 3; ++pick) {
        const RunActionMask g = mask_of(rc);
        bool acted = false;
        for (uint16_t i = 0; i < kMasterDeckCap && !acted; ++i) {
            if (g.can_choose_master_deck[i]) {
                step(rc, make_action(ActionVerb::CHOOSE,
                                     static_cast<uint8_t>(i)));
                acted = true;
            }
        }
        ASSERT_TRUE(acted);
    }
    EXPECT_EQ(rc.run.boss_chest.screen,
              static_cast<uint8_t>(BossChestScreen::DONE));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::BOSS_TREASURE));
}

TEST(BossChest, PickingCallingBellRunsItsConfirmAndRewardScreensHere) {
    RunController rc = at_boss_chest();
    rc.run.boss_chest.relics[2] = static_cast<uint16_t>(RelicId::CALLING_BELL);
    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    step(rc, make_action(ActionVerb::CHOOSE, 2));

    ASSERT_EQ(rc.run.boss_chest.screen,
              static_cast<uint8_t>(BossChestScreen::EQUIP_GRID));
    ASSERT_EQ(rc.neow.grid_mode,
              static_cast<uint8_t>(NeowGridMode::CONFIRM_CALLING_BELL));
    RunActionMask m = mask_of(rc);
    ASSERT_TRUE(m.can_proceed) << "a choice-free confirmation grid";

    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    EXPECT_EQ(rc.run.boss_chest.screen,
              static_cast<uint8_t>(BossChestScreen::EQUIP_ITEM_REWARD));
    EXPECT_EQ(rc.rewards.count, 3) << "three fixed-tier relic rows";
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::BOSS_TREASURE))
        << "the equip screens live INSIDE the chest room, not in a new phase";

    m = mask_of(rc);
    EXPECT_TRUE(m.can_claim_reward[0]);
    step(rc, make_action(ActionVerb::CHOOSE, 0));
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    EXPECT_EQ(rc.run.boss_chest.screen,
              static_cast<uint8_t>(BossChestScreen::DONE));
}

// =============================================================================
// The starter swap: instantObtain(player, 0, true), not obtain()
// =============================================================================

// Capture STS212624 (A20 Ironclad, the Act-2 boss chest at floor 34) picked
// Black Blood from [Mark of Pain, Sacred Bark, Black Blood] with ten relics
// owned and Burning Blood at [0]. The game's own dump one record later shows
// TEN relics with Black Blood at [0]; the sim had ELEVEN -- Burning Blood still
// at [0], Black Blood appended at [10] -- and every later record carried those
// three fields. The mechanism: AbstractRelic.bossObtainLogic
// (AbstractRelic.java:391-398) skips obtain() for the four starter-swap ids,
// and BossRelicSelectScreen.relicObtainLogic (BossRelicSelectScreen.java
// :196-198) calls `r.instantObtain(AbstractDungeon.player, 0, true)` instead,
// whose `slot < p.relics.size()` arm is `p.relics.set(slot, this)`
// (AbstractRelic.java:230-234): an in-place replacement -- no count change, no
// loseRelic, onEquip run (:240-243). The pick branch has no act test, so the
// same arm serves the Act-1 chest; this pins it where the helper parks.
TEST(BossChest, BlackBloodReplacesTheStarterInSlotZeroWithoutCostingASlot) {
    RunController rc = at_boss_chest();
    // Two relics behind the starter, one with a non-default counter, so index
    // preservation (trap 8) and an untouched neighbour are both observable.
    ASSERT_EQ(acquire_relic(rc.run, rc.combat.misc_rng, RelicId::TINY_CHEST),
              RelicAcquireResult::ACQUIRED);
    ASSERT_EQ(acquire_relic(rc.run, rc.combat.misc_rng, RelicId::ANCHOR),
              RelicAcquireResult::ACQUIRED);
    rc.run.relics[rc.run.relic_count - 2].counter = 3;  // Tiny Chest, mid-cycle
    ASSERT_EQ(rc.run.relics[0].relic_id,
              static_cast<uint16_t>(RelicId::BURNING_BLOOD))
        << "Ironclad.getStartingRelics puts Burning Blood at [0]";
    const RunState before = rc.run;
    const uint8_t count_before = before.relic_count;
    ASSERT_GE(count_before, 3);

    rc.run.boss_chest.relics[2] = static_cast<uint16_t>(RelicId::BLACK_BLOOD);
    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    step(rc, make_action(ActionVerb::CHOOSE, 2));

    EXPECT_EQ(rc.run.relic_count, count_before)
        << "relics.set(0, this), not relics.add(this): no slot is spent";
    EXPECT_EQ(rc.run.relics[0].relic_id,
              static_cast<uint16_t>(RelicId::BLACK_BLOOD));
    EXPECT_EQ(rc.run.relics[0].counter, -1)
        << "a fresh relic object: AbstractRelic's default, not a leftover";
    EXPECT_FALSE(owns(rc.run, RelicId::BURNING_BLOOD))
        << "the starter is dropped by being overwritten; :233 is a plain set, "
           "with no loseRelic and no onUnequip";
    for (uint8_t i = 1; i < count_before; ++i) {
        EXPECT_EQ(rc.run.relics[i].relic_id, before.relics[i].relic_id)
            << "slot " << int{i} << " moved";
        EXPECT_EQ(rc.run.relics[i].counter, before.relics[i].counter)
            << "slot " << int{i} << "'s counter changed";
    }
    EXPECT_EQ(rc.run.relics[count_before].relic_id,
              static_cast<uint16_t>(RelicId::NONE))
        << "nothing was appended past the old end";
    EXPECT_EQ(rc.run.boss_chest.chose_relic, 1);
    EXPECT_EQ(rc.run.boss_chest.screen,
              static_cast<uint8_t>(BossChestScreen::DONE))
        << "Black Blood has no onEquip screen; the room is finished";
    // And the gate the swap closes: BlackBlood.canSpawn is hasRelic("Burning
    // Blood") (BlackBlood.java:39-41), which the replacement just falsified.
    RelicSpawnContext ctx{};
    fill_boss_spawn_gates(rc.run, ctx);
    EXPECT_FALSE(ctx.has_burning_blood);
}

// The negative control for the arm above: every OTHER boss relic goes through
// bossObtainLogic's obtain() (AbstractRelic.java:393 -> :277-291,
// `player.relics.add(this)` at :288) and lands at the END, behind the starter.
// Mark of Pain is the capture's own slot-0 offer.
TEST(BossChest, AnOrdinaryBossRelicStillAppendsBehindTheStarter) {
    RunController rc = at_boss_chest();
    const uint8_t count_before = rc.run.relic_count;
    ASSERT_EQ(rc.run.relics[0].relic_id,
              static_cast<uint16_t>(RelicId::BURNING_BLOOD));

    rc.run.boss_chest.relics[0] = static_cast<uint16_t>(RelicId::MARK_OF_PAIN);
    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    step(rc, make_action(ActionVerb::CHOOSE, 0));

    EXPECT_EQ(rc.run.relic_count, count_before + 1);
    EXPECT_EQ(rc.run.relics[0].relic_id,
              static_cast<uint16_t>(RelicId::BURNING_BLOOD))
        << "the starter stays in slot 0 for a non-swap pick";
    EXPECT_EQ(rc.run.relics[count_before].relic_id,
              static_cast<uint16_t>(RelicId::MARK_OF_PAIN));
}

// =============================================================================
// canSpawn: trap 9 and the starter-swap gates
// =============================================================================

// Put `id` at the front of the boss pool so the first pop must consider it.
void front_load_boss_pool(RunState& rs, RelicId id) {
    for (uint8_t i = 0; i < rs.relic_pool_count[kBossPool]; ++i) {
        if (rs.relic_pools[kBossPool][i] == static_cast<uint16_t>(id)) {
            for (uint8_t k = i; k > 0; --k) {
                rs.relic_pools[kBossPool][k] = rs.relic_pools[kBossPool][k - 1];
            }
            rs.relic_pools[kBossPool][0] = static_cast<uint16_t>(id);
            return;
        }
    }
    ADD_FAILURE() << "relic not in the boss pool";
}

TEST(BossChest, Trap9EctoplasmIsAcceptedAtTheActOneBossChest) {
    RunController rc = run_begin(kSeed, kA20);
    front_load_boss_pool(rc.run, RelicId::ECTOPLASM);
    RunState rs = rc.run;
    ASSERT_EQ(rs.act, 1) << "dungeonTransitionSetup runs only AFTER this "
                            "chest's proceed, so actNum is still the OUTGOING "
                            "act while the chest is built -- that ordering is "
                            "what makes Ectoplasm offerable here";
    const uint8_t before = rs.relic_pool_count[kBossPool];

    const BossChestState chest = roll_boss_chest(rs);

    EXPECT_EQ(chest.relics[0], static_cast<uint16_t>(RelicId::ECTOPLASM))
        << "Ectoplasm.canSpawn is actNum <= 1 (Ectoplasm.java:54-57)";
    EXPECT_EQ(rs.relic_pool_count[kBossPool], before - 3) << "no rejection";
}

TEST(BossChest, Trap9EctoplasmIsRejectedAndConsumedAtTheActTwoBossChest) {
    RunController rc = run_begin(kSeed, kA20);
    front_load_boss_pool(rc.run, RelicId::ECTOPLASM);
    RunState rs = rc.run;
    rs.act = 2;
    const uint8_t before = rs.relic_pool_count[kBossPool];

    const BossChestState chest = roll_boss_chest(rs);

    for (int i = 0; i < kBossChestOfferCount; ++i) {
        EXPECT_NE(chest.relics[i], static_cast<uint16_t>(RelicId::ECTOPLASM));
    }
    // The rejected relic is popped and CONSUMED before the reroute
    // (AbstractDungeon.java:804-806), so three offers cost FOUR entries.
    EXPECT_EQ(rs.relic_pool_count[kBossPool], before - 4)
        << "a canSpawn rejection costs the pool an extra entry -- the "
           "pool-cursor divergence trap 9 exists to catch";
}

TEST(BossChest, BlackBloodIsRejectedForeverAfterTheNeowBossSwap) {
    RunController rc = run_begin(kSeed, kA20);
    // The swap removes Burning Blood first (NeowReward.java:244-245), and
    // BlackBlood.canSpawn is hasRelic("Burning Blood") -- so it is dead for the
    // rest of the run, at this chest as much as at the swap.
    ASSERT_TRUE(lose_relic(rc.run, RelicId::BURNING_BLOOD));
    front_load_boss_pool(rc.run, RelicId::BLACK_BLOOD);
    RunState rs = rc.run;
    const uint8_t before = rs.relic_pool_count[kBossPool];

    const BossChestState chest = roll_boss_chest(rs);

    for (int i = 0; i < kBossChestOfferCount; ++i) {
        EXPECT_NE(chest.relics[i], static_cast<uint16_t>(RelicId::BLACK_BLOOD));
    }
    EXPECT_EQ(rs.relic_pool_count[kBossPool], before - 4);
}

TEST(BossChest, ExhaustedBossPoolOffersThreeCirclets) {
    RunController rc = run_begin(kSeed, kA20);
    RunState rs = rc.run;
    rs.relic_pool_count[kBossPool] = 0;

    const BossChestState chest = roll_boss_chest(rs);

    // The BOSS empty arm returns the key "Red Circlet", which RelicLibrary
    // never registered, so getRelic falls through to Circlet
    // (RelicLibrary.java:610-626).
    for (int i = 0; i < kBossChestOfferCount; ++i) {
        EXPECT_EQ(chest.relics[i], static_cast<uint16_t>(RelicId::CIRCLET));
    }
    EXPECT_EQ(rs.relic_pool_count[kBossPool], 0);
}

// =============================================================================
// The sapphire-key pin (s2-tasks.md deferred obligation) and the hook pass
// =============================================================================

TEST(BossChest, NeverAppendsTheSapphireKeyRow) {
    RunController rc = at_boss_chest();
    // The gate AbstractChest.open:95-97 reads is `isFinalActAvailable &&
    // !hasSapphireKey`. Arrange the most permissive possible version of it --
    // no key held at all -- so a false negative cannot come from the condition.
    ASSERT_EQ(rc.run.keys, 0);

    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));

    EXPECT_EQ(rc.run.keys, 0)
        << "BossChest.open(boolean) FULLY OVERRIDES AbstractChest.open with no "
           "super call (BossChest.java:49-63), so the sapphire-key append at "
           "AbstractChest.java:95-97 is unreachable from the boss chest";
    EXPECT_EQ(rc.rewards.count, 0)
        << "and so are addRelicToRewards / addGoldToRewards / the curse";
}

TEST(BossChest, FiresNoRelicChestHooks) {
    RunController rc = at_boss_chest();
    // The three registered chest-hook relics, all held at once.
    ASSERT_EQ(acquire_relic(rc.run, rc.combat.misc_rng, RelicId::MATRYOSHKA),
              RelicAcquireResult::ACQUIRED);
    ASSERT_EQ(acquire_relic(rc.run, rc.combat.misc_rng, RelicId::NLOTHS_MASK),
              RelicAcquireResult::ACQUIRED);
    const RunState before = rc.run;
    const uint16_t deck_before = rc.run.master_deck_count;

    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));

    EXPECT_EQ(rc.run.relic_rng.counter, before.relic_rng.counter)
        << "Matryoshka's extra-relic roll would have drawn relicRng; the Java "
           "`continue`s past it at BossChest.java:53";
    EXPECT_EQ(rc.run.master_deck_count, deck_before)
        << "Cursed Key's curse would have joined the deck; its own body guards "
           "!bossChest";
    // Since schema v8 the open ITSELF legitimately writes two RunState bytes --
    // the chest screen and the `seen` reveal flag now live in rs.boss_chest --
    // so the no-op claim is everything BUT those two: normalize them and the
    // whole struct must byte-compare clean.
    RunState after = rc.run;
    after.boss_chest.screen = before.boss_chest.screen;
    after.boss_chest.seen = before.boss_chest.seen;
    EXPECT_EQ(std::memcmp(&before, &after, sizeof before), 0)
        << "the whole boss-chest hook pass is a no-op";
}

// =============================================================================
// The observation layer
// =============================================================================

TEST(BossChest, UnopenedOffersAreHiddenAndOpenedOnesArePublic) {
    RunController rc = at_boss_chest();
    PublicView pv{};
    encode_public_view(rc, pv);
    EXPECT_EQ(pv.run_phase, static_cast<uint8_t>(RunPhase::BOSS_TREASURE));
    EXPECT_EQ(pv.chest_opened, 0);
    for (int i = 0; i < kBossChestOfferCount; ++i) {
        EXPECT_EQ(pv.boss_relic_choice_reserved[i], 0)
            << "the relics are drawn but UNSEEN -- the same masking trap the "
               "ordinary chest's contents are";
    }

    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    encode_public_view(rc, pv);
    EXPECT_EQ(pv.chest_opened, 1);
    for (int i = 0; i < kBossChestOfferCount; ++i) {
        EXPECT_EQ(pv.boss_relic_choice_reserved[i], rc.run.boss_chest.relics[i]);
    }

    // A skip closes the chest but cannot unsee it.
    step(rc, make_action(ActionVerb::CHOOSE, kChooseCancelGrid));
    encode_public_view(rc, pv);
    EXPECT_EQ(pv.chest_opened, 1)
        << "`seen`, not `screen`, is the reveal flag";
    EXPECT_EQ(pv.boss_relic_choice_reserved[0], rc.run.boss_chest.relics[0]);
}

TEST(BossChest, AnUnopenedChestsTwinEncodesIdentically) {
    RunController truth = at_boss_chest();
    RunController twin = truth;
    make_hidden_twin(twin, /*sampler_seed=*/99);

    PublicView a{};
    PublicView b{};
    encode_public_view(truth, a);
    encode_public_view(twin, b);
    EXPECT_EQ(std::memcmp(&a, &b, sizeof a), 0)
        << "an unopened chest's offers are hidden, so a particle that redrew "
           "them must still encode to the same public view";
}

TEST(BossChest, AnOpenedChestsTwinKeepsTheOffers) {
    RunController truth = at_boss_chest();
    step(truth, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    RunController twin = truth;
    make_hidden_twin(twin, /*sampler_seed=*/99);

    EXPECT_EQ(std::memcmp(twin.run.boss_chest.relics, truth.run.boss_chest.relics,
                          sizeof truth.run.boss_chest.relics),
              0)
        << "an opened chest's offers are on screen -- a pure copy";
    PublicView a{};
    PublicView b{};
    encode_public_view(truth, a);
    encode_public_view(twin, b);
    EXPECT_EQ(std::memcmp(&a, &b, sizeof a), 0);
}

// =============================================================================
// The act terminal seam
// =============================================================================

TEST(BossChest, ProceedIsTheActTerminalSeam) {
    RunController rc = at_boss_chest();
    step(rc, make_action(ActionVerb::CHOOSE, kChooseOpenChest));
    step(rc, make_action(ActionVerb::CHOOSE, 0));
    for (int guard = 0; guard < 20 &&
                        rc.run.boss_chest.screen !=
                            static_cast<uint8_t>(BossChestScreen::DONE);
         ++guard) {
        const RunActionMask m = mask_of(rc);
        bool acted = false;
        for (uint16_t i = 0; i < kMasterDeckCap && !acted; ++i) {
            if (m.can_choose_master_deck[i]) {
                step(rc, make_action(ActionVerb::CHOOSE,
                                     static_cast<uint8_t>(i)));
                acted = true;
            }
        }
        if (!acted && m.can_proceed) {
            step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
            acted = true;
        }
        ASSERT_TRUE(acted);
    }

    const StepResult res =
        step_with_result(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));

    // S2.12 filled the seam: the chest's proceed runs dungeonTransitionSetup and
    // constructs the next dungeon, landing on ITS map. The chest is a boundary,
    // not a terminal -- only the Act-3 boss ends the run (s2-design §1), and
    // run_is_victory() moved there with it. The transition's own semantics are
    // pinned in act_transition_test.cpp; what this test owns is the SEAM's
    // contract -- the room's state cleared, the phase set, `res` filled.
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));
    EXPECT_EQ(rc.run.act, 2);
    EXPECT_FALSE(run_is_victory(rc));
    EXPECT_FALSE(res.terminal);
    EXPECT_EQ(res.reward, 0.0f);
    EXPECT_EQ(rc.run.boss_chest.relics[0], 0) << "the room's state was cleared";
    EXPECT_EQ(rc.rewards.count, 0) << "and so was any equip reward screen";
}

TEST(BossChest, TheProceedOpensTheNextActsMapRatherThanAnEmptyMask) {
    // The property the old terminal test guarded, restated for the transition:
    // the boss column has no outgoing map edges, so a proceed that landed at
    // MAP_CHOICE without regenerating the map would advertise an EMPTY mask
    // while claiming not to be terminal -- the soak's no_legal_moves. That is
    // the seed-116 regression, and it stays guarded here.
    RunController rc = at_boss_chest();
    step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::MAP_CHOICE));

    RunActionMask m{};
    legal_actions(rc, m);
    bool any = m.can_choose_boss;
    for (int x = 0; x < kMapCols; ++x) any = any || m.can_choose_node[x];
    EXPECT_TRUE(any) << "a non-terminal phase must offer something";
}

TEST(BossChest, IllegalChoicesAreNonCorruptingNoOps) {
    RunController rc = at_boss_chest();
    const RunState before = rc.run;
    const BossChestState chest_before = rc.run.boss_chest;

    for (uint8_t a0 : {uint8_t{7}, uint8_t{40}, uint8_t{200},
                       kChooseCancelGrid, kChooseSing}) {
        step(rc, make_action(ActionVerb::CHOOSE, a0));
    }
    step(rc, make_action(ActionVerb::END_TURN));

    EXPECT_EQ(std::memcmp(&before, &rc.run, sizeof before), 0);
    EXPECT_EQ(std::memcmp(&chest_before, &rc.run.boss_chest, sizeof chest_before), 0);
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::BOSS_TREASURE));
}

}  // namespace
}  // namespace sts::engine
