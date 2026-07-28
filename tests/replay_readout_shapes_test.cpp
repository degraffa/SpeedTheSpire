// Tier-2 tests for the two SHAPE decisions the `--treasure` and `--event`
// read-outs make before any comparison happens
// (tools/oracle_bridge/replay/src/readout_shapes.hpp).
//
// WHY THESE ARE UNIT TESTS AND NOT "IT RAN CLEAN ON THE CAPTURES". Both
// functions exist to let a read-out call something benign, which is the one
// class of bug a green campaign run cannot show you: a rule that elides too
// much simply reports zero-diff. The chest key-row rule in particular is asked
// to ignore a row that appears on EVERY Act-1 chest open, so the interesting
// cases are the ones the two captured chests do not contain -- a key row on the
// wrong screen, and a missing one.
//
// Provenance, all read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled:
//   AbstractChest.open                    AbstractChest.java:62-102
//   AbstractRoom.addSapphireKey           AbstractRoom.java:545-547
//   AbstractRoom.removeOneRelicFromRewards AbstractRoom.java:549-559
//   RewardItem(RewardItem, RewardType)    RewardItem.java:86-93
//   RewardItem.claimReward RELIC / KEY    RewardItem.java:290-305, 316-325

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "readout_shapes.hpp"

namespace {

using sts::replay::CaptureRewardRow;
using sts::replay::ClaimTarget;
using sts::replay::join_capture_event;
using sts::replay::KeyRowContext;
using sts::replay::map_reward_claim;
using sts::replay::strip_sapphire_key_row;

CaptureRewardRow gold_row(int amount) {
    CaptureRewardRow r;
    r.type = "GOLD";
    r.gold = amount;
    return r;
}

CaptureRewardRow relic_row(std::string id) {
    CaptureRewardRow r;
    r.type = "RELIC";
    r.relic_id = std::move(id);
    return r;
}

CaptureRewardRow key_row(std::string linked) {
    CaptureRewardRow r;
    r.type = "SAPPHIRE_KEY";
    r.link_id = std::move(linked);
    return r;
}

KeyRowContext chest_ctx() {
    KeyRowContext c;
    c.chest_open = true;
    return c;
}

// --- the key row that IS expected ------------------------------------------

// The shape STS00054's floor-9 SmallChest actually has: one RELIC row, then the
// linked key. It must be elided, and the surviving rows must be exactly the
// simulator's.
TEST(SapphireKeyRow, TrailingLinkedKeyOnAChestOpenIsIgnored) {
    const std::vector<CaptureRewardRow> rows = {relic_row("Bag of Preparation"),
                                                key_row("Bag of Preparation")};
    const auto v = strip_sapphire_key_row(rows, chest_ctx());
    ASSERT_TRUE(v.ok) << v.problem;
    EXPECT_EQ(v.key_index, 1);
    ASSERT_EQ(v.rows.size(), 1u);
    EXPECT_EQ(v.rows[0].type, "RELIC");
    EXPECT_EQ(v.rows[0].relic_id, "Bag of Preparation");
}

// A gold-bearing chest puts the gold row first; the key still trails the relic.
TEST(SapphireKeyRow, GoldRowSurvivesTheElision) {
    const std::vector<CaptureRewardRow> rows = {
        gold_row(31), relic_row("Vajra"), key_row("Vajra")};
    const auto v = strip_sapphire_key_row(rows, chest_ctx());
    ASSERT_TRUE(v.ok) << v.problem;
    ASSERT_EQ(v.rows.size(), 2u);
    EXPECT_EQ(v.rows[0].type, "GOLD");
    EXPECT_EQ(v.rows[0].gold, 31);
    EXPECT_EQ(v.rows[1].type, "RELIC");
}

// --- the key row that is NOT expected ---------------------------------------

// A key row cannot appear on a combat reward: AbstractChest.open is its only
// producer. Reporting it is the point -- eliding it here would hide a real
// translator or capture defect.
TEST(SapphireKeyRow, KeyOnANonTreasureRewardScreenIsFlagged) {
    const std::vector<CaptureRewardRow> rows = {gold_row(14), relic_row("Vajra"),
                                                key_row("Vajra")};
    KeyRowContext ctx;  // chest_open stays false
    const auto v = strip_sapphire_key_row(rows, ctx);
    EXPECT_FALSE(v.ok);
    EXPECT_NE(v.problem.find("NOT a treasure-chest open"), std::string::npos)
        << v.problem;
}

// The mirror case: a chest open whose relic row carries no key, with neither of
// the two legitimate explanations in play.
TEST(SapphireKeyRow, MissingKeyOnAChestOpenIsFlagged) {
    const std::vector<CaptureRewardRow> rows = {relic_row("Vajra")};
    const auto v = strip_sapphire_key_row(rows, chest_ctx());
    EXPECT_FALSE(v.ok);
    EXPECT_NE(v.problem.find("NO trailing SAPPHIRE_KEY"), std::string::npos)
        << v.problem;
}

// Explanation one: the run already holds a key, so !Settings.hasSapphireKey is
// false and the :95-96 branch does not fire.
TEST(SapphireKeyRow, MissingKeyIsBenignOnceTheRunHoldsOne) {
    const std::vector<CaptureRewardRow> rows = {relic_row("Vajra")};
    KeyRowContext ctx = chest_ctx();
    ctx.already_has_key = true;
    const auto v = strip_sapphire_key_row(rows, ctx);
    EXPECT_TRUE(v.ok) << v.problem;
    EXPECT_EQ(v.key_index, -1);
    EXPECT_EQ(v.rows.size(), 1u);
}

// ...and the same run may not then be handed a key row anyway.
TEST(SapphireKeyRow, KeyRowAfterTheRunAlreadyHoldsOneIsFlagged) {
    const std::vector<CaptureRewardRow> rows = {relic_row("Vajra"), key_row("Vajra")};
    KeyRowContext ctx = chest_ctx();
    ctx.already_has_key = true;
    const auto v = strip_sapphire_key_row(rows, ctx);
    EXPECT_FALSE(v.ok);
    EXPECT_NE(v.problem.find("already holds a sapphire key"), std::string::npos)
        << v.problem;
}

// Explanation two: N'loth's Mask deleted the relic row AND the linked key with
// it (AbstractRoom.java:549-559). Both rows are gone together -- a mask that
// left a relic row behind is NOT this case.
TEST(SapphireKeyRow, NlothsMaskRemovalTakesRelicAndKeyTogether) {
    const std::vector<CaptureRewardRow> gone = {gold_row(31)};
    KeyRowContext ctx = chest_ctx();
    ctx.nloths_mask_fired = true;
    EXPECT_TRUE(strip_sapphire_key_row(gone, ctx).ok);

    const std::vector<CaptureRewardRow> relic_kept = {relic_row("Vajra")};
    const auto v = strip_sapphire_key_row(relic_kept, ctx);
    EXPECT_FALSE(v.ok) << "a surviving RELIC row means the mask did not fire";
}

// Structural checks: the key is appended LAST and is linked in both directions.
TEST(SapphireKeyRow, NonTrailingOrUnlinkedKeyIsFlagged) {
    const std::vector<CaptureRewardRow> not_last = {
        relic_row("Vajra"), key_row("Vajra"), gold_row(31)};
    EXPECT_FALSE(strip_sapphire_key_row(not_last, chest_ctx()).ok);

    const std::vector<CaptureRewardRow> wrong_link = {relic_row("Vajra"),
                                                      key_row("Bag of Preparation")};
    const auto v = strip_sapphire_key_row(wrong_link, chest_ctx());
    EXPECT_FALSE(v.ok);
    EXPECT_NE(v.problem.find("links to"), std::string::npos) << v.problem;

    const std::vector<CaptureRewardRow> two = {relic_row("Vajra"), key_row("Vajra"),
                                               key_row("Vajra")};
    EXPECT_FALSE(strip_sapphire_key_row(two, chest_ctx()).ok);
}

// --- what a claim on such a screen MEANS ------------------------------------

// Claiming the base relic keeps it and marks the key done
// (RewardItem.java:298-300): index 0 both sides.
TEST(RewardClaimMapping, ClaimingTheBaseRelicMapsThroughTheElidedIndexSpace) {
    const std::vector<CaptureRewardRow> rows = {gold_row(31), relic_row("Vajra"),
                                                key_row("Vajra")};
    const auto m = map_reward_claim(rows, /*key_index=*/2, /*choice=*/1);
    EXPECT_EQ(m.what, ClaimTarget::SIM_ROW);
    EXPECT_EQ(m.sim_index, 1);
}

// Claiming the KEY marks the linked relic isDone/ignoreReward
// (RewardItem.java:317-322), so the run never obtains it: there is no sim row
// to claim, and the harness must say so rather than pick a neighbouring index.
// This is the shape STS00054 actually captured.
TEST(RewardClaimMapping, ClaimingTheKeyAbandonsTheLinkedRelic) {
    const std::vector<CaptureRewardRow> rows = {relic_row("Bag of Preparation"),
                                                key_row("Bag of Preparation")};
    const auto m = map_reward_claim(rows, /*key_index=*/1, /*choice=*/1);
    EXPECT_EQ(m.what, ClaimTarget::ABANDONS_RELIC);
    EXPECT_EQ(m.sim_index, -1);
}

// A row after the elided key shifts down by one; an out-of-range choice is
// named rather than clamped.
TEST(RewardClaimMapping, IndicesAfterTheKeyShiftAndOutOfRangeIsRefused) {
    std::vector<CaptureRewardRow> rows = {relic_row("Vajra"), key_row("Vajra"),
                                          gold_row(31)};
    const auto after = map_reward_claim(rows, /*key_index=*/1, /*choice=*/2);
    EXPECT_EQ(after.what, ClaimTarget::SIM_ROW);
    EXPECT_EQ(after.sim_index, 1);

    EXPECT_EQ(map_reward_claim(rows, 1, 3).what, ClaimTarget::OUT_OF_RANGE);
    EXPECT_EQ(map_reward_claim(rows, 1, -1).what, ClaimTarget::OUT_OF_RANGE);

    // With no key row present the index space is the identity.
    rows = {gold_row(31), relic_row("Vajra")};
    const auto plain = map_reward_claim(rows, /*key_index=*/-1, /*choice=*/1);
    EXPECT_EQ(plain.what, ClaimTarget::SIM_ROW);
    EXPECT_EQ(plain.sim_index, 1);
}

// --- the event identity join -------------------------------------------------

// The join key is the class's static ID, which is events.yaml's `game_id`.
TEST(EventJoin, CaptureIdsJoinToTheRegistry) {
    EXPECT_EQ(join_capture_event("Big Fish").id,
              static_cast<uint16_t>(sts::registry::EventId::BIG_FISH));
    EXPECT_TRUE(join_capture_event("Big Fish").problem.empty());
    // The six ids these campaigns show whose DISPLAY name differs from the id
    // -- the reason the join may not go through `event_name`.
    EXPECT_NE(join_capture_event("Liars Game").id, 0u);          // "The Ssssserpent"
    EXPECT_NE(join_capture_event("Golden Wing").id, 0u);         // "Wing Statue"
    EXPECT_NE(join_capture_event("FaceTrader").id, 0u);          // "Face Trader"
    EXPECT_NE(join_capture_event("Fountain of Cleansing").id, 0u);
    EXPECT_NE(join_capture_event("Bonfire Elementals").id, 0u);  // "Bonfire Spirits"
    // The game's own misspelling is the id; the UI spells it correctly.
    EXPECT_NE(join_capture_event("Transmorgrifier").id, 0u);
    EXPECT_EQ(join_capture_event("Transmogrifier").id, 0u);
}

// An unknown id is schema drift and must fail loud, exactly as the translator's
// own join does -- never silently become "no event".
TEST(EventJoin, UnknownNameFailsLoud) {
    const auto v = join_capture_event("Face Trader");  // the localized NAME
    EXPECT_EQ(v.id, 0u);
    EXPECT_NE(v.problem.find("unknown event id"), std::string::npos) << v.problem;

    const auto empty = join_capture_event("");
    EXPECT_EQ(empty.id, 0u);
    EXPECT_FALSE(empty.problem.empty());

    const auto invented = join_capture_event("Definitely Not An Event");
    EXPECT_EQ(invented.id, 0u);
    EXPECT_NE(invented.problem.find("schema drift"), std::string::npos)
        << invented.problem;
}

// Neow is an EVENT screen with a hard-coded sentinel id and no events.yaml row
// (it is in no act's pool). Joining it must be refused with the reason, not
// treated as an unknown id and not silently accepted.
TEST(EventJoin, NeowSentinelIsRefusedByName) {
    const auto v = join_capture_event("Neow Event");
    EXPECT_EQ(v.id, 0u);
    EXPECT_NE(v.problem.find("--neow"), std::string::npos) << v.problem;
}

}  // namespace
