#pragma once

// mk_board.hpp -- reading a captured Match and Keep! board out of the artifact,
// and comparing it against the board the simulator dealt.
//
// INTERNAL header, same rationale as `command_map.hpp` and `readout_shapes.hpp`
// next door (conventions "Where a new header goes"): its consumers are this
// tool's `main.cpp` and its own gtest. It is split out for the same reason
// those two were -- the decode below is the one place a read-out can quietly
// mis-align the capture's twelve card slots with the simulator's and then
// "compare" nothing, and until it was separable the only way to see that go
// wrong was to read a campaign's output by eye. Everything here is plain data
// and plain strings: no JSON, no engine types, no artifact needed to test it.
//
// ---------------------------------------------------------------------------
// WHAT THE CAPTURE ACTUALLY SHOWS, and why it takes a decode at all.
//
// The board is dealt in the event's CONSTRUCTOR (GremlinMatchGame.java:55-61):
// `initializeCards()` builds six identities, duplicates each with
// `makeStatEquivalentCopy` so the list is [six originals][six copies], and then
// `Collections.shuffle(this.cards.group, new Random(miscRng.randomLong()))`
// permutes all twelve. `this.cards.group` order after that shuffle is what the
// simulator's `EventDialogState.board[]` holds, index for index.
//
// The capture does NOT expose that list. What it exposes is the oracle fork's
// event-screen option list, and three separate things happen to it on the way
// out (`ChoiceScreenUtils.getEventScreenChoices`, and
// `patches/GremlinMatchGamePatch.java` -- both vendored under
// tools/oracle_bridge/communicationmod-oracle/):
//
//   1. CARDS ARE RE-INDEXED BY SCREEN POSITION, NOT BY GROUP INDEX.
//      `InitializeCardsPatch` records, once, at construction:
//          int target_x = i % 4;  int target_y = i % 3;
//          position = target_x + 4 * target_y;
//      which is `placeCards`' own layout arithmetic (GremlinMatchGame.java:
//      278-285) read back as a 4-wide grid index. For i = 0..11 that is
//      [0, 5, 10, 3, 4, 9, 2, 7, 8, 1, 6, 11] -- a PERMUTATION, and the patch's
//      own comment says so. A `card7` label therefore does NOT name
//      `cards.group[7]`; it names the card that landed in the 8th grid cell,
//      which is `cards.group[10]`. Comparing the two index spaces directly is
//      the single mistake this header exists to make impossible, and it is not
//      a mistake a passing read-out would announce: eight of the twelve
//      positions are wrong under the identity mapping, so it would fail loudly
//      on real data -- but a read-out that only compared MULTISETS would pass
//      while proving nothing about order at all.
//
//   2. THE LIST IS COMPACTED, AND CARRIES NO POSITION FOR A REVEALED CARD.
//      `getOrderedCards()` copies `cards.group`, sorts by that stored position,
//      and then `removeIf(c -> !c.isFlipped)` -- i.e. it offers only the cards
//      that are still ON THE BOARD and still FACE DOWN, ascending by position.
//      Two kinds of card drop out: the first pick of an attempt (the game sets
//      `isFlipped = false` at :190 and it stays face up until the pair
//      resolves), and both cards of a matched pair (`cards.group.remove`,
//      :221-222). A `choose N` from the driver indexes into THAT compacted
//      list, not into the board.
//      Worse for a naive reader: the label of a card that has ever been flipped
//      is its `cardID`, not `card<position>` -- `revealedCards` is a permanent
//      set (`CardIdentificationPatch` inserts at the `isFlipped = false` write,
//      so a card stays "revealed" after a mismatch flips it back down). So a
//      revealed entry carries its IDENTITY but not its POSITION.
//      The decode below recovers the position anyway, exactly, because the list
//      is SORTED: entry j of a record is the j-th smallest still-offered
//      position, and the offered set is derivable from the capture's own picks.
//      Every hidden entry then re-states its position in its label, so the
//      whole reconstruction is CHECKED against the artifact at every entry of
//      every record rather than assumed -- `decode_match_grid` fails loud on
//      the first label that disagrees.
//
//   3. A MATCHED PAIR'S IDENTITY IS NEVER LABELLED.
//      A matched pair leaves `cards.group` in the same frame it is revealed, so
//      it never appears in a later option list under its name. Its identity is
//      recoverable from the run's MASTER DECK instead -- the chosen copy is
//      obtained through `ShowCardAndObtainEffect` (:224) -- which is why
//      `decode_match_grid` takes the deck delta's size: it is also the only
//      thing that can settle the FIFTH attempt's outcome, since no grid record
//      follows it (the next screen is "Leave").
//
// WHAT THAT LEAVES COMPARABLE, stated precisely rather than optimistically:
// every position the capture ever named is compared by IDENTITY, position for
// position; every attempt whose outcome is known is compared as a PAIR
// PREDICATE (`board[a] == board[b]`), which constrains positions the capture
// never named; and the multiset of identities the run obtained is compared
// against the deck delta. The positions that are never flipped and never
// matched are constrained by the pairing invariant alone. `MatchDealDiff`
// counts each kind separately so a read-out line states what it actually
// checked instead of implying twelve.

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace sts::replay {

// --- 1. The group-index <-> screen-position permutation ----------------------

inline constexpr int kMatchBoardSlots = 12;

// `cards.group` index -> screen position, verbatim from
// GremlinMatchGamePatch.InitializeCardsPatch (which is placeCards'
// GremlinMatchGame.java:280-281 arithmetic, read as a 4-wide grid index).
[[nodiscard]] constexpr int match_screen_position(int group_index) noexcept {
    return (group_index % 4) + 4 * (group_index % 3);
}

namespace detail {
[[nodiscard]] constexpr std::array<int, kMatchBoardSlots> build_group_index() noexcept {
    std::array<int, kMatchBoardSlots> inv{};
    for (int i = 0; i < kMatchBoardSlots; ++i)
        inv[static_cast<std::size_t>(match_screen_position(i))] = i;
    return inv;
}
[[nodiscard]] constexpr bool is_a_permutation() noexcept {
    std::array<int, kMatchBoardSlots> seen{};
    for (int i = 0; i < kMatchBoardSlots; ++i) {
        const int p = match_screen_position(i);
        if (p < 0 || p >= kMatchBoardSlots) return false;
        if (seen[static_cast<std::size_t>(p)] != 0) return false;
        seen[static_cast<std::size_t>(p)] = 1;
    }
    return true;
}
}  // namespace detail

inline constexpr std::array<int, kMatchBoardSlots> kMatchGroupIndex =
    detail::build_group_index();

// Screen position -> `cards.group` index. The inverse of the above, which is
// the direction a read-out actually needs: the capture speaks positions, the
// simulator's `board[]` speaks group indices.
[[nodiscard]] constexpr int match_group_index(int screen_position) noexcept {
    return kMatchGroupIndex[static_cast<std::size_t>(screen_position)];
}

// The permutation is load-bearing in BOTH directions, so both are pinned at
// compile time as well as in the gtest. The literal is the fork patch's own
// comment ("the positions of the cards in the result array are: ...").
static_assert(detail::is_a_permutation(),
              "match_screen_position must be a bijection on 0..11 -- if it is "
              "not, some board slot is unreachable and the decode silently "
              "compares the wrong card");
static_assert(match_screen_position(0) == 0 && match_screen_position(1) == 5 &&
                  match_screen_position(2) == 10 && match_screen_position(3) == 3 &&
                  match_screen_position(4) == 4 && match_screen_position(5) == 9 &&
                  match_screen_position(6) == 2 && match_screen_position(7) == 7 &&
                  match_screen_position(8) == 8 && match_screen_position(9) == 1 &&
                  match_screen_position(10) == 6 && match_screen_position(11) == 11,
              "the group->position table must be [0,5,10,3,4,9,2,7,8,1,6,11] "
              "(GremlinMatchGamePatch.InitializeCardsPatch)");

// --- 2. Decoding the captured grid walk --------------------------------------

// One captured PLAY-screen record: the option labels exactly as
// `screen_state.options[].label` gave them, in order, plus the `choose N` this
// record's command carried. `choice` indexes `labels`.
struct MatchGridRecord {
    std::vector<std::string> labels;
    int choice = -1;
};

// One resolved (or half-resolved) attempt, in SCREEN POSITIONS.
struct MatchAttempt {
    int first = -1;
    int second = -1;  // -1 when the capture stops mid-attempt
    bool matched = false;
    bool outcome_known = false;
};

struct MatchBoardObservation {
    bool ok = false;
    std::string problem;  // empty iff ok

    // Identity at each screen position; "" for a position the capture never
    // named. Values are the game's `cardID`, i.e. registry `game_id`.
    std::array<std::string, kMatchBoardSlots> revealed{};
    int revealed_count = 0;

    std::vector<MatchAttempt> attempts;
    // Per grid record, the screen positions the capture offered, ascending.
    // Parallel to the input; this is what a per-round walk compares against.
    std::vector<std::vector<int>> offered;
};

// `obtained_count` is the number of cards the run's master deck gained across
// the whole interaction -- the only evidence for the LAST attempt's outcome,
// since no grid record follows it. Pass a negative value when the delta is not
// available; the last attempt is then left `outcome_known == false` rather than
// guessed.
[[nodiscard]] inline MatchBoardObservation decode_match_grid(
    const std::vector<MatchGridRecord>& grids, int obtained_count) {
    MatchBoardObservation obs;
    obs.offered.reserve(grids.size());

    // Positions still on the board AND still face down -- exactly what
    // `getOrderedCards()` offers. Everything starts face down (`placeCards`
    // sets `isFlipped = true` on all twelve, :283).
    std::vector<int> present;
    present.reserve(kMatchBoardSlots);
    for (int p = 0; p < kMatchBoardSlots; ++p) present.push_back(p);

    std::vector<int> pending;  // the current attempt's picks, in order
    int matches_seen = 0;

    for (std::size_t g = 0; g < grids.size(); ++g) {
        const MatchGridRecord& rec = grids[g];
        if (rec.labels.size() != present.size()) {
            obs.problem =
                "grid record " + std::to_string(g) + " offers " +
                std::to_string(rec.labels.size()) + " option(s) but " +
                std::to_string(present.size()) +
                " card(s) are still on the board and face down; "
                "getOrderedCards() offers exactly those "
                "(GremlinMatchGamePatch.getOrderedCards)";
            return obs;
        }
        obs.offered.push_back(present);

        for (std::size_t j = 0; j < rec.labels.size(); ++j) {
            const int pos = present[j];
            const std::string& label = rec.labels[j];
            // A hidden card re-states its own position -- which is the check
            // that the whole reconstruction is right, at every entry.
            bool hidden = label.rfind("card", 0) == 0 && label.size() > 4;
            if (hidden) {
                for (std::size_t c = 4; c < label.size(); ++c)
                    if (label[c] < '0' || label[c] > '9') hidden = false;
            }
            if (hidden) {
                if (std::stoi(label.substr(4)) != pos) {
                    obs.problem = "grid record " + std::to_string(g) +
                                  " entry " + std::to_string(j) + " is labelled \"" +
                                  label + "\" but the sorted offered set puts screen "
                                  "position " + std::to_string(pos) +
                                  " there; the capture's own board order and this "
                                  "decode disagree";
                    return obs;
                }
                continue;
            }
            // A revealed card carries its cardID and no position, so the
            // position is the one the sort just gave it.
            std::string& slot = obs.revealed[static_cast<std::size_t>(pos)];
            if (slot.empty()) {
                slot = label;
                ++obs.revealed_count;
            } else if (slot != label) {
                obs.problem = "screen position " + std::to_string(pos) +
                              " was revealed as \"" + slot + "\" and later as \"" +
                              label + "\"; a card's identity does not change";
                return obs;
            }
        }

        if (rec.choice < 0 || rec.choice >= static_cast<int>(present.size())) {
            obs.problem = "grid record " + std::to_string(g) + " carries `choose " +
                          std::to_string(rec.choice) + "` but offers " +
                          std::to_string(present.size()) + " option(s)";
            return obs;
        }
        const int picked = present[static_cast<std::size_t>(rec.choice)];
        pending.push_back(picked);
        present.erase(present.begin() + rec.choice);

        if (pending.size() != 2) continue;

        MatchAttempt at;
        at.first = pending[0];
        at.second = pending[1];
        if (g + 1 < grids.size()) {
            // The next record's SIZE settles it, with no identity guesswork: a
            // matched pair is gone from `cards.group` for good (:221-222), a
            // mismatched pair flips face down again (:228-229) and is offered
            // once more.
            const std::size_t next = grids[g + 1].labels.size();
            if (next == present.size()) {
                at.matched = true;
                at.outcome_known = true;
                ++matches_seen;
            } else if (next == present.size() + 2) {
                at.matched = false;
                at.outcome_known = true;
                present.push_back(at.first);
                present.push_back(at.second);
                std::sort(present.begin(), present.end());
            } else {
                obs.problem =
                    "after the attempt ending at grid record " + std::to_string(g) +
                    " the next record offers " + std::to_string(next) +
                    " option(s); a match leaves " + std::to_string(present.size()) +
                    " and a miss leaves " + std::to_string(present.size() + 2);
                return obs;
            }
        } else if (obtained_count >= 0) {
            // The last attempt: nothing follows it on screen, so the master
            // deck is the only witness.
            if (obtained_count == matches_seen + 1) {
                at.matched = true;
                at.outcome_known = true;
                ++matches_seen;
            } else if (obtained_count == matches_seen) {
                at.matched = false;
                at.outcome_known = true;
            } else {
                obs.problem =
                    "the run's deck gained " + std::to_string(obtained_count) +
                    " card(s) but " + std::to_string(matches_seen) +
                    " attempt(s) matched before the last one; the last attempt "
                    "can account for at most one more";
                return obs;
            }
        }
        obs.attempts.push_back(at);
        pending.clear();
    }

    if (!pending.empty()) {
        MatchAttempt at;
        at.first = pending[0];
        obs.attempts.push_back(at);  // truncated: no second pick, no outcome
    }

    obs.ok = true;
    return obs;
}

// --- 3. The comparison against the simulator's dealt board --------------------

struct MatchDealDiff {
    bool ok = false;
    std::vector<std::string> problems;
    int identity_checks = 0;  // positions the capture named and this compared
    int pair_checks = 0;      // attempts compared as a match/miss predicate
};

// `sim_board` is the simulator's dealt board indexed by SCREEN POSITION (the
// caller does the `match_group_index` hop), each entry a registry `game_id`.
[[nodiscard]] inline MatchDealDiff compare_match_deal(
    const MatchBoardObservation& obs,
    const std::array<std::string, kMatchBoardSlots>& sim_board) {
    MatchDealDiff d;
    if (!obs.ok) {
        d.problems.push_back("the capture's board could not be decoded: " +
                             obs.problem);
        return d;
    }

    // --- the deal's structural invariant, stated as the Java actually states it
    //
    // `initializeCards` (GremlinMatchGame.java:63-91) builds SIX deal slots and
    // then duplicates the whole list -- `retVal2` collects one
    // `makeStatEquivalentCopy` per slot and `retVal.addAll(retVal2)` (:84-86)
    // appends them. So what the twelve slots are guaranteed to be is a PERFECT
    // PAIRING: every identity appears an EVEN number of times. That, and only
    // that, is what the duplication proves.
    //
    // WHAT IT DOES NOT PROVE, and what this check used to assume: that the six
    // deal slots are DISTINCT. On the `ascensionLevel >= 15` branch (:66-71)
    // two of them are `AbstractDungeon.returnRandomCurse()` calls, back to back
    // at :70-71, with no dedup between them and no dedup afterwards. Two equal
    // curses is an ordinary outcome of that pair of draws, and the board it
    // deals legitimately holds FOUR copies of one curse. STS00683 of the G6
    // campaign is that board (four `Regret`), and the old `n != 2` test scored
    // it as a divergence -- while the read-out's own numbers said the board was
    // right: five capture-named positions compared, five attempt outcomes
    // reproduced, ten grid rounds walked. A wrong board cannot do that.
    //
    // The relaxation is exactly the collision the Java admits and no wider:
    //
    //   * every position filled, and every count EVEN (the pairing itself);
    //   * every count 2 or 4 -- a count of 6 would need THREE equal deal slots,
    //     and only two of the six can collide (the other four come from
    //     disjoint pools: RARE, UNCOMMON, a COMMON or colorless UNCOMMON, and
    //     `getStartCardForEvent`, :72-79);
    //   * at MOST ONE identity at 4, for the same reason -- one collision is
    //     all the two curse draws can produce.
    //
    // A board of twelve copies of one card, three copies of anything, or two
    // different quadruples still fails loud.
    int quads = 0;
    for (int p = 0; p < kMatchBoardSlots; ++p) {
        if (sim_board[static_cast<std::size_t>(p)].empty()) {
            d.problems.push_back("the sim's board has no card at screen position " +
                                 std::to_string(p));
            continue;
        }
        int n = 0;
        int first = p;
        for (int q = 0; q < kMatchBoardSlots; ++q) {
            if (sim_board[static_cast<std::size_t>(q)] !=
                sim_board[static_cast<std::size_t>(p)])
                continue;
            if (q < first) first = q;
            ++n;
        }
        if (n == 4 && first == p) ++quads;
        if (n == 2 || n == 4) continue;
        d.problems.push_back(
            "the sim's board holds " + std::to_string(n) + " copies of \"" +
            sim_board[static_cast<std::size_t>(p)] +
            "\"; initializeCards deals six slots and duplicates the whole list, "
            "so every identity appears 2 times -- or 4 when the two "
            "returnRandomCurse draws collide (GremlinMatchGame.java:70-71,84-86)");
        break;  // one report is enough; the whole board is suspect
    }
    if (quads > 1) {
        d.problems.push_back(
            "the sim's board holds " + std::to_string(quads) +
            " identities at 4 copies each; only the two adjacent "
            "returnRandomCurse draws can collide, so at most one identity can "
            "be dealt twice (GremlinMatchGame.java:70-71)");
    }

    for (int p = 0; p < kMatchBoardSlots; ++p) {
        const std::string& want = obs.revealed[static_cast<std::size_t>(p)];
        if (want.empty()) continue;
        ++d.identity_checks;
        const std::string& got = sim_board[static_cast<std::size_t>(p)];
        if (got != want)
            d.problems.push_back("screen position " + std::to_string(p) +
                                 " (board slot " + std::to_string(match_group_index(p)) +
                                 "): capture dealt \"" + want + "\", sim dealt \"" +
                                 got + "\"");
    }

    for (std::size_t i = 0; i < obs.attempts.size(); ++i) {
        const MatchAttempt& a = obs.attempts[i];
        if (!a.outcome_known || a.second < 0) continue;
        ++d.pair_checks;
        const bool sim_match = sim_board[static_cast<std::size_t>(a.first)] ==
                               sim_board[static_cast<std::size_t>(a.second)];
        if (sim_match != a.matched)
            d.problems.push_back(
                "attempt " + std::to_string(i + 1) + " flipped screen positions " +
                std::to_string(a.first) + " and " + std::to_string(a.second) +
                ": the capture " + (a.matched ? "MATCHED" : "did not match") +
                " but the sim dealt \"" + sim_board[static_cast<std::size_t>(a.first)] +
                "\" and \"" + sim_board[static_cast<std::size_t>(a.second)] + "\"");
    }

    d.ok = d.problems.empty();
    return d;
}

}  // namespace sts::replay
