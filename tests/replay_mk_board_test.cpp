// B4.13 -- the Match and Keep! board decode
// (tools/oracle_bridge/replay/src/mk_board.hpp).
//
// WHY THIS SUITE EXISTS. Everything here is a place the `--event` deal read-out
// can be WRONG WITHOUT FAILING. The capture never dumps the dealt board; it
// dumps the fork's event option list, which is compacted (only cards still on
// the board and still face down), sorted by SCREEN POSITION rather than by
// board slot, and labels an already-flipped card by identity with no position.
// A decode that mis-maps the permutation, mis-tracks the compaction, or assumes
// the sorted order without checking it against the labels still produces twelve
// comparisons and a "clean deal" line -- it just compares the wrong cards. So
// the alignment gets tests of its own rather than only a green campaign run.
//
// The fixtures below are the STS00212 floor-4 interaction from campaign
// `b4x_greedy_pilot_20260728T041406Z_claude01`, transcribed from the artifact's
// `screen_state.options[].label` lists, plus hand-built degenerate cases the
// corpus does not contain.

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

#include "mk_board.hpp"

using sts::replay::compare_match_deal;
using sts::replay::decode_match_grid;
using sts::replay::kMatchBoardSlots;
using sts::replay::match_group_index;
using sts::replay::match_screen_position;
using sts::replay::MatchBoardObservation;
using sts::replay::MatchDealDiff;
using sts::replay::MatchGridRecord;

namespace {

// `GremlinMatchGamePatch.InitializeCardsPatch`'s own comment: "If 0 is top left
// and 11 is bottom right, the positions of the cards in the result array are:
// [0, 5, 10, 3, 4, 9, 2, 7, 8, 1, 6, 11]".
constexpr int kExpectedPositions[kMatchBoardSlots] = {0, 5, 10, 3, 4, 9,
                                                      2, 7, 8,  1, 6, 11};

MatchGridRecord grid(std::vector<std::string> labels, int choice) {
    MatchGridRecord g;
    g.labels = std::move(labels);
    g.choice = choice;
    return g;
}

// A full twelve-slot face-down board, as the first PLAY record always shows it.
std::vector<std::string> all_hidden(const std::vector<int>& positions) {
    std::vector<std::string> out;
    for (int p : positions) out.push_back("card" + std::to_string(p));
    return out;
}

// STS00212 floor 4: five attempts, one match at the start, one at the end.
// Screen positions flipped, in order: (10,9) MATCH, (4,0) miss, (6,3) miss,
// (1,6) miss, (0,1) MATCH.
std::vector<MatchGridRecord> sts00212_grids() {
    return {
        // attempt 1: choose 10 -> position 10; choose 9 -> position 9; matched.
        grid(all_hidden({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}), 10),
        grid(all_hidden({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11}), 9),
        // attempt 2: 9 and 10 are gone. choose 4 -> position 4; choose 0 -> 0.
        grid(all_hidden({0, 1, 2, 3, 4, 5, 6, 7, 8, 11}), 4),
        grid(all_hidden({0, 1, 2, 3, 5, 6, 7, 8, 11}), 0),
        // attempt 3: 0 and 4 came back, now named. choose entry 6 -> position 6.
        grid({"Decay", "card1", "card2", "card3", "Fire Breathing", "card5", "card6",
              "card7", "card8", "card11"},
             6),
        grid({"Decay", "card1", "card2", "card3", "Fire Breathing", "card5", "card7",
              "card8", "card11"},
             3),
        // attempt 4: choose entry 1 -> position 1; choose entry 5 -> position 6.
        grid({"Decay", "card1", "card2", "Bash", "Fire Breathing", "card5", "Berserk",
              "card7", "card8", "card11"},
             1),
        grid({"Decay", "card2", "Bash", "Fire Breathing", "card5", "Berserk", "card7",
              "card8", "card11"},
             5),
        // attempt 5: choose entry 0 -> position 0; choose entry 0 -> position 1.
        grid({"Decay", "Decay", "card2", "Bash", "Fire Breathing", "card5", "Berserk",
              "card7", "card8", "card11"},
             0),
        grid({"Decay", "card2", "Bash", "Fire Breathing", "card5", "Berserk", "card7",
              "card8", "card11"},
             0),
    };
}

// The board STS00212's Match and Keep actually dealt, by SCREEN POSITION, as
// `--event --verbose` prints it. Seven of the twelve are pinned by the capture:
// 0, 1, 3, 4 and 6 are named outright on screen, and 9/10 are the pair the
// first attempt matched, identified by the True Grit the master deck gained
// (a matched pair leaves `cards.group` before it can be named). The other five
// -- 2, 5, 7, 8, 11 -- are the simulator's, constrained only by the pairing
// invariant; they are here so the fixture is a whole board, and no assertion
// below depends on their particular values.
//
// The six identities are exactly what initializeCards deals at ascension >= 15
// (GremlinMatchGame.java:66-71, 79): Berserk (RARE), Fire Breathing (UNCOMMON),
// True Grit (COMMON), Decay and Shame (the two curses) and Bash
// (Ironclad.getStartCardForEvent).
std::array<std::string, kMatchBoardSlots> sts00212_board() {
    return {"Decay",     "Decay",     "Shame",     "Bash",
            "Fire Breathing", "Fire Breathing", "Berserk", "Shame",
            "Bash",      "True Grit", "True Grit", "Berserk"};
}

}  // namespace

// --- the permutation ---------------------------------------------------------

TEST(MatchBoardPositions, MatchesTheForkPatchTable) {
    for (int i = 0; i < kMatchBoardSlots; ++i)
        EXPECT_EQ(match_screen_position(i), kExpectedPositions[i]) << "group index " << i;
}

TEST(MatchBoardPositions, IsNotTheIdentityMapping) {
    // The whole point of the header: half the slots move. A read-out that
    // skipped the hop would still "compare twelve cards" -- and would agree
    // with the capture on the six that happen to be fixed points, which is
    // exactly the sort of half-right result nobody looks at twice.
    int moved = 0;
    for (int i = 0; i < kMatchBoardSlots; ++i)
        if (match_screen_position(i) != i) ++moved;
    EXPECT_EQ(moved, 6);
}

TEST(MatchBoardPositions, GroupIndexIsTheExactInverse) {
    for (int i = 0; i < kMatchBoardSlots; ++i)
        EXPECT_EQ(match_group_index(match_screen_position(i)), i);
    for (int p = 0; p < kMatchBoardSlots; ++p)
        EXPECT_EQ(match_screen_position(match_group_index(p)), p);
}

// --- the decode --------------------------------------------------------------

TEST(MatchBoardDecode, RecoversEveryRevealedPositionFromARealCapture) {
    const MatchBoardObservation obs = decode_match_grid(sts00212_grids(), 2);
    ASSERT_TRUE(obs.ok) << obs.problem;
    EXPECT_EQ(obs.revealed_count, 5);
    EXPECT_EQ(obs.revealed[0], "Decay");
    EXPECT_EQ(obs.revealed[1], "Decay");
    EXPECT_EQ(obs.revealed[3], "Bash");
    EXPECT_EQ(obs.revealed[4], "Fire Breathing");
    EXPECT_EQ(obs.revealed[6], "Berserk");
    // A matched pair leaves cards.group before it can be named on screen.
    EXPECT_TRUE(obs.revealed[9].empty());
    EXPECT_TRUE(obs.revealed[10].empty());
}

TEST(MatchBoardDecode, RecoversEveryAttemptsPositionsAndOutcome) {
    const MatchBoardObservation obs = decode_match_grid(sts00212_grids(), 2);
    ASSERT_TRUE(obs.ok) << obs.problem;
    ASSERT_EQ(obs.attempts.size(), 5u);
    const int first[5] = {10, 4, 6, 1, 0};
    const int second[5] = {9, 0, 3, 6, 1};
    const bool matched[5] = {true, false, false, false, true};
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(obs.attempts[i].first, first[i]) << "attempt " << i;
        EXPECT_EQ(obs.attempts[i].second, second[i]) << "attempt " << i;
        EXPECT_TRUE(obs.attempts[i].outcome_known) << "attempt " << i;
        EXPECT_EQ(obs.attempts[i].matched, matched[i]) << "attempt " << i;
    }
}

TEST(MatchBoardDecode, TheDeckDeltaIsWhatSettlesTheLastAttempt) {
    // Nothing follows the fifth attempt on screen, so the master deck is the
    // only witness. One gain before it, two in total -> the last one matched.
    const MatchBoardObservation two = decode_match_grid(sts00212_grids(), 2);
    ASSERT_TRUE(two.ok) << two.problem;
    EXPECT_TRUE(two.attempts[4].matched);

    // The same walk with only the FIRST attempt's card in the delta: the fifth
    // attempt must then read as a miss, not as a match.
    const MatchBoardObservation one = decode_match_grid(sts00212_grids(), 1);
    ASSERT_TRUE(one.ok) << one.problem;
    EXPECT_TRUE(one.attempts[4].outcome_known);
    EXPECT_FALSE(one.attempts[4].matched);

    // With no delta available the outcome is left UNKNOWN rather than guessed.
    const MatchBoardObservation none = decode_match_grid(sts00212_grids(), -1);
    ASSERT_TRUE(none.ok) << none.problem;
    EXPECT_FALSE(none.attempts[4].outcome_known);
}

TEST(MatchBoardDecode, ADeltaThatCannotBeExplainedFailsLoud) {
    // Four matches claimed by the deck, one seen on screen before the last
    // attempt: the last attempt can account for at most one more.
    const MatchBoardObservation obs = decode_match_grid(sts00212_grids(), 4);
    EXPECT_FALSE(obs.ok);
    EXPECT_NE(obs.problem.find("at most one more"), std::string::npos) << obs.problem;
}

TEST(MatchBoardDecode, OfferedSetsAreTheCompactedPositionLists) {
    const MatchBoardObservation obs = decode_match_grid(sts00212_grids(), 2);
    ASSERT_TRUE(obs.ok) << obs.problem;
    ASSERT_EQ(obs.offered.size(), 10u);
    EXPECT_EQ(obs.offered[0], (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}));
    EXPECT_EQ(obs.offered[1], (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11}));
    // The matched pair is gone for good; the mismatched pair comes back.
    EXPECT_EQ(obs.offered[2], (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 11}));
    EXPECT_EQ(obs.offered[4], (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 11}));
}

TEST(MatchBoardDecode, AMislabelledPositionFailsLoud) {
    // The check that makes the reconstruction evidence rather than assumption:
    // every hidden entry re-states its own position, so a list whose order does
    // not match the derived offered set is caught at the first entry.
    std::vector<MatchGridRecord> g = sts00212_grids();
    g[0].labels[3] = "card7";  // position 3's slot now claims to be position 7
    const MatchBoardObservation obs = decode_match_grid(g, 2);
    EXPECT_FALSE(obs.ok);
    EXPECT_NE(obs.problem.find("card7"), std::string::npos) << obs.problem;
}

TEST(MatchBoardDecode, AWrongSizedRecordFailsLoud) {
    std::vector<MatchGridRecord> g = sts00212_grids();
    g[1].labels.pop_back();
    const MatchBoardObservation obs = decode_match_grid(g, 2);
    EXPECT_FALSE(obs.ok);
    EXPECT_NE(obs.problem.find("still on the board"), std::string::npos) << obs.problem;
}

TEST(MatchBoardDecode, AnOutOfRangeChooseFailsLoud) {
    std::vector<MatchGridRecord> g = sts00212_grids();
    g[0].choice = 12;
    const MatchBoardObservation obs = decode_match_grid(g, 2);
    EXPECT_FALSE(obs.ok);
    EXPECT_NE(obs.problem.find("choose 12"), std::string::npos) << obs.problem;
}

TEST(MatchBoardDecode, AnIdentityThatChangesFailsLoud) {
    std::vector<MatchGridRecord> g = sts00212_grids();
    g[6].labels[0] = "Berserk";  // position 0 was "Decay" one record earlier
    const MatchBoardObservation obs = decode_match_grid(g, 2);
    EXPECT_FALSE(obs.ok);
    EXPECT_NE(obs.problem.find("does not change"), std::string::npos) << obs.problem;
}

TEST(MatchBoardDecode, AnUnclassifiableAttemptFailsLoud) {
    // Neither "both gone" (match) nor "both back" (miss): exactly one returned.
    std::vector<MatchGridRecord> g = sts00212_grids();
    g[2].labels = all_hidden({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11});
    const MatchBoardObservation obs = decode_match_grid(g, 2);
    EXPECT_FALSE(obs.ok);
    EXPECT_NE(obs.problem.find("a match leaves"), std::string::npos) << obs.problem;
}

TEST(MatchBoardDecode, ATruncatedWalkKeepsTheHalfAttemptWithoutAnOutcome) {
    std::vector<MatchGridRecord> g = sts00212_grids();
    g.resize(3);  // one full attempt plus one lone pick
    const MatchBoardObservation obs = decode_match_grid(g, 1);
    ASSERT_TRUE(obs.ok) << obs.problem;
    ASSERT_EQ(obs.attempts.size(), 2u);
    EXPECT_TRUE(obs.attempts[0].outcome_known);
    EXPECT_EQ(obs.attempts[1].first, 4);
    EXPECT_EQ(obs.attempts[1].second, -1);
    EXPECT_FALSE(obs.attempts[1].outcome_known);
}

// --- the comparison ----------------------------------------------------------

TEST(MatchDealCompare, TheRealBoardIsZeroDiff) {
    const MatchBoardObservation obs = decode_match_grid(sts00212_grids(), 2);
    ASSERT_TRUE(obs.ok) << obs.problem;
    const MatchDealDiff d = compare_match_deal(obs, sts00212_board());
    ASSERT_TRUE(d.ok) << (d.problems.empty() ? "" : d.problems.front());
    EXPECT_EQ(d.identity_checks, 5);
    EXPECT_EQ(d.pair_checks, 5);
}

TEST(MatchDealCompare, AWrongIdentityAtANamedPositionIsCaught) {
    const MatchBoardObservation obs = decode_match_grid(sts00212_grids(), 2);
    ASSERT_TRUE(obs.ok) << obs.problem;
    std::array<std::string, kMatchBoardSlots> board = sts00212_board();
    // Swap the two identities the capture named at positions 3 and 4. The
    // multiset is untouched and so is the pairing invariant (Bash moves to
    // 4/8, Fire Breathing to 3/5), so only a POSITIONAL comparison catches it.
    std::swap(board[3], board[4]);
    const MatchDealDiff d = compare_match_deal(obs, board);
    EXPECT_FALSE(d.ok);
    ASSERT_FALSE(d.problems.empty());
    EXPECT_NE(d.problems.front().find("screen position 3"), std::string::npos)
        << d.problems.front();
    EXPECT_NE(d.problems.front().find("board slot 3"), std::string::npos)
        << d.problems.front();
}

TEST(MatchDealCompare, TheIdentityMappingWouldNotPass) {
    // The concrete regression this header exists for: comparing the capture's
    // screen positions against the sim's board slots WITHOUT the permutation.
    const MatchBoardObservation obs = decode_match_grid(sts00212_grids(), 2);
    ASSERT_TRUE(obs.ok) << obs.problem;
    std::array<std::string, kMatchBoardSlots> shuffled{};
    const std::array<std::string, kMatchBoardSlots> real = sts00212_board();
    for (int i = 0; i < kMatchBoardSlots; ++i)
        shuffled[static_cast<std::size_t>(i)] =
            real[static_cast<std::size_t>(match_screen_position(i))];
    const MatchDealDiff d = compare_match_deal(obs, shuffled);
    EXPECT_FALSE(d.ok);
}

TEST(MatchDealCompare, APairOutcomeReachesPositionsTheCaptureNeverNamed) {
    const MatchBoardObservation obs = decode_match_grid(sts00212_grids(), 2);
    ASSERT_TRUE(obs.ok) << obs.problem;
    std::array<std::string, kMatchBoardSlots> board = sts00212_board();
    // Positions 9 and 10 are never named on screen -- they are the pair the
    // FIRST attempt matched, and a matched pair leaves the board before it can
    // be labelled. Break their equality while keeping every named position and
    // the pairing invariant intact: only the attempt predicate can see this.
    std::swap(board[2], board[9]);  // Shame <-> True Grit
    const MatchDealDiff d = compare_match_deal(obs, board);
    EXPECT_FALSE(d.ok);
    ASSERT_FALSE(d.problems.empty());
    EXPECT_NE(d.problems.front().find("attempt 1"), std::string::npos)
        << d.problems.front();
    EXPECT_NE(d.problems.front().find("MATCHED"), std::string::npos)
        << d.problems.front();
}

TEST(MatchDealCompare, ABoardThatLostThePairingInvariantIsCaught) {
    const MatchBoardObservation obs = decode_match_grid(sts00212_grids(), 2);
    ASSERT_TRUE(obs.ok) << obs.problem;
    std::array<std::string, kMatchBoardSlots> board = sts00212_board();
    board[11] = "Anger";  // one lonely Berserk, one lonely Anger
    const MatchDealDiff d = compare_match_deal(obs, board);
    EXPECT_FALSE(d.ok);
    bool named = false;
    for (const std::string& p : d.problems)
        if (p.find("returnRandomCurse") != std::string::npos) named = true;
    EXPECT_TRUE(named) << (d.problems.empty() ? "" : d.problems.front());
}

// --- the shape check admits the collision the Java admits, and no more -------
//
// `initializeCards` (GremlinMatchGame.java:63-91) duplicates its whole six-slot
// list, so the twelve slots are a PERFECT PAIRING -- every identity an even
// number of times. That is all the duplication proves. It does NOT prove the
// six slots are distinct: on the `ascensionLevel >= 15` branch two of them are
// back-to-back `AbstractDungeon.returnRandomCurse()` calls (:70-71) with no
// dedup, so a legitimate board can hold FOUR copies of one curse.
//
// STS00683 of the G6 campaign dealt exactly that (four `Regret`) and the old
// `n != 2` test scored it DIFF -- while the same read-out's numbers said the
// board was right: five capture-named positions compared, five attempt outcomes
// reproduced, ten grid rounds walked. A wrong board cannot do that.
TEST(MatchDealCompare, TheDoubleCurseDealIsALegalBoard) {
    const MatchBoardObservation obs = decode_match_grid(sts00212_grids(), 2);
    ASSERT_TRUE(obs.ok) << obs.problem;
    std::array<std::string, kMatchBoardSlots> board = sts00212_board();
    // The two curse slots drew the same curse: Shame's pair becomes Decay's, so
    // Decay stands at four copies. Positions 2 and 7 are the two the capture
    // never named, so nothing positional changes and every attempt outcome
    // stays what it was.
    board[2] = "Decay";
    board[7] = "Decay";
    const MatchDealDiff d = compare_match_deal(obs, board);
    ASSERT_TRUE(d.ok) << d.problems.front();
    EXPECT_EQ(d.identity_checks, 5);
    EXPECT_EQ(d.pair_checks, 5);
}

TEST(MatchDealCompare, ThreeCopiesIsStillImpossible) {
    const MatchBoardObservation obs = decode_match_grid(sts00212_grids(), 2);
    ASSERT_TRUE(obs.ok) << obs.problem;
    std::array<std::string, kMatchBoardSlots> board = sts00212_board();
    board[7] = "Decay";  // Decay x3, Shame x1 -- not a pairing at all
    const MatchDealDiff d = compare_match_deal(obs, board);
    EXPECT_FALSE(d.ok);
    ASSERT_FALSE(d.problems.empty());
    EXPECT_NE(d.problems.front().find("3 copies"), std::string::npos)
        << d.problems.front();
}

// Only the two curse draws can collide -- the other four slots come from
// disjoint pools (RARE, UNCOMMON, COMMON-or-colorless, getStartCardForEvent) --
// so ONE quadruple is the most a legal deal can carry.
TEST(MatchDealCompare, TwoQuadruplesAreStillImpossible) {
    const MatchBoardObservation obs = decode_match_grid(sts00212_grids(), 2);
    ASSERT_TRUE(obs.ok) << obs.problem;
    std::array<std::string, kMatchBoardSlots> board = sts00212_board();
    board[2] = "Decay";       // Decay x4
    board[7] = "Decay";
    board[9] = "Berserk";     // Berserk x4 as well
    board[10] = "Berserk";
    const MatchDealDiff d = compare_match_deal(obs, board);
    EXPECT_FALSE(d.ok);
    bool named = false;
    for (const std::string& p : d.problems)
        if (p.find("4 copies each") != std::string::npos) named = true;
    EXPECT_TRUE(named) << (d.problems.empty() ? "" : d.problems.front());
}

TEST(MatchDealCompare, ABoardOfOneIdentityIsStillImpossible) {
    const MatchBoardObservation obs = decode_match_grid(sts00212_grids(), 2);
    ASSERT_TRUE(obs.ok) << obs.problem;
    std::array<std::string, kMatchBoardSlots> board;
    board.fill("Regret");
    const MatchDealDiff d = compare_match_deal(obs, board);
    EXPECT_FALSE(d.ok);
    ASSERT_FALSE(d.problems.empty());
    EXPECT_NE(d.problems.front().find("12 copies"), std::string::npos)
        << d.problems.front();
}

TEST(MatchDealCompare, AFailedDecodeIsReportedRatherThanCountedClean) {
    MatchBoardObservation bad;
    bad.ok = false;
    bad.problem = "synthetic";
    const MatchDealDiff d = compare_match_deal(bad, sts00212_board());
    EXPECT_FALSE(d.ok);
    EXPECT_EQ(d.identity_checks, 0);
    EXPECT_EQ(d.pair_checks, 0);
}
