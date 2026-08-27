// Pile operations -- draw / reshuffle / exhaust / energy (design doc §3.3, §9;
// §10 trap 2).
//
// The reshuffle tests use the ALREADY golden-vector-tested primitives
// (random_long over RngStream, JdkRandom, jdk_shuffle) as an independent oracle:
// the engine's shuffle_discard_into_draw must reproduce, bit-for-bit, a hand-
// wired "draw one random_long -> seed a JdkRandom -> Fisher-Yates the discard"
// computed here from a COPY of the same starting stream. No new JVM golden
// vectors are needed because those primitives are already proven against the JVM.
//
// Provenance for the behaviors under test: AbstractPlayer.draw (AbstractPlayer.
// java:1632-1665), DrawCardAction.update (DrawCardAction.java:63-127; the
// corrected up-front hand-size cap), EmptyDeckShuffleAction.update
// (EmptyDeckShuffleAction.java:42-64), CardGroup.shuffle (CardGroup.java:565-567),
// Soul.shuffle (Soul.java:90-102).

#include "sts/engine/piles.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/cards.hpp"  // card_def / card_cost / card_flags (cost reset)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

// --- Helpers ----------------------------------------------------------------

// A minimal combat with a fixed, known shuffle_rng seed (so every reshuffle is
// reproducible from a copied stream). Other streams are irrelevant here.
CombatState make_combat(int64_t shuffle_seed = 0x5EEDF00DLL) {
    CombatState s{};
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.player_energy = 3;
    s.shuffle_rng = from_seed(shuffle_seed);
    return s;
}

void set_draw(CombatState& s, const std::vector<CardPoolIndex>& pile) {
    s.draw_count = static_cast<uint8_t>(pile.size());
    for (std::size_t i = 0; i < pile.size(); ++i) s.draw[i] = pile[i];
}

void set_discard(CombatState& s, const std::vector<CardPoolIndex>& pile) {
    s.discard_count = static_cast<uint8_t>(pile.size());
    for (std::size_t i = 0; i < pile.size(); ++i) s.discard[i] = pile[i];
}

void set_hand(CombatState& s, const std::vector<CardPoolIndex>& pile) {
    s.hand_count = static_cast<uint8_t>(pile.size());
    for (std::size_t i = 0; i < pile.size(); ++i) s.hand[i] = pile[i];
}

std::vector<CardPoolIndex> draw_vec(const CombatState& s) {
    return std::vector<CardPoolIndex>(s.draw, s.draw + s.draw_count);
}

// Independent oracle: reproduce shuffle_discard_into_draw's permutation using
// the golden-tested primitives, starting from a COPY of `stream`.
std::vector<CardPoolIndex> expected_reshuffle(RngStream stream,
                                              std::vector<CardPoolIndex> discard) {
    const int64_t seed = random_long(stream);  // one draw off the copied stream
    JdkRandom rng(seed);
    jdk_shuffle(std::span<CardPoolIndex>(discard.data(), discard.size()), rng);
    return discard;  // draw pile becomes the shuffled discard array, same order
}

ActionQueueItem op(Opcode code, int32_t amount, uint8_t tgt = kActorPlayer) {
    ActionQueueItem i{};
    i.opcode = static_cast<uint16_t>(code);
    i.src = kActorPlayer;
    i.tgt = tgt;
    i.amount = amount;
    return i;
}

// --- Reshuffle: permutation matches golden JDK shuffle ----------------------

TEST(PilesReshuffle, MatchesGoldenJdkShuffleForKnownStreamState) {
    CombatState s = make_combat();
    const std::vector<CardPoolIndex> discard = {10, 11, 12, 13, 14, 15, 16};
    set_discard(s, discard);
    // draw pile empty (the deck-exhausted state a reshuffle fires from).

    const RngStream before = s.shuffle_rng;
    shuffle_discard_into_draw(s);

    const std::vector<CardPoolIndex> expected = expected_reshuffle(before, discard);
    EXPECT_EQ(draw_vec(s), expected);
    EXPECT_EQ(s.draw_count, discard.size());
    EXPECT_EQ(s.discard_count, 0u);

    // The stream must have advanced exactly as one random_long would: identical
    // to the copy after a single draw (state AND counter).
    RngStream ref = before;
    (void)random_long(ref);
    EXPECT_EQ(std::memcmp(&s.shuffle_rng, &ref, sizeof(RngStream)), 0);
}

// Reshuffle appends onto a non-empty draw tail in shuffled order (the general
// addToTop-to-END case), leaving the pre-existing draw pile below it.
TEST(PilesReshuffle, AppendsShuffledDiscardOntoDrawTail) {
    CombatState s = make_combat();
    const std::vector<CardPoolIndex> existing = {1, 2};   // stays at the bottom
    const std::vector<CardPoolIndex> discard = {20, 21, 22, 23, 24};
    set_draw(s, existing);
    set_discard(s, discard);

    const RngStream before = s.shuffle_rng;
    shuffle_discard_into_draw(s);

    std::vector<CardPoolIndex> expected = existing;
    const std::vector<CardPoolIndex> shuffled = expected_reshuffle(before, discard);
    expected.insert(expected.end(), shuffled.begin(), shuffled.end());
    EXPECT_EQ(draw_vec(s), expected);
    EXPECT_EQ(s.discard_count, 0u);
}

// --- shuffleRng counter advances by exactly 1 per shuffle -------------------

TEST(PilesReshuffle, ShuffleRngCounterAdvancesByExactlyOne) {
    for (const int n : {2, 5, 10, 50, 128}) {
        CombatState s = make_combat();
        std::vector<CardPoolIndex> discard;
        for (int i = 0; i < n; ++i) discard.push_back(static_cast<CardPoolIndex>(i));
        set_discard(s, discard);

        const int32_t before = s.shuffle_rng.counter;
        shuffle_discard_into_draw(s);
        EXPECT_EQ(s.shuffle_rng.counter, before + 1)
            << "discard size " << n
            << ": Fisher-Yates swaps must consume JDK-internal draws, not RngStream";
    }
}

// Empty discard pile is a no-op and draws NO random_long (counter unchanged).
TEST(PilesReshuffle, EmptyDiscardIsNoOpAndDrawsNoRandomLong) {
    CombatState s = make_combat();
    const RngStream before = s.shuffle_rng;
    shuffle_discard_into_draw(s);
    EXPECT_EQ(s.draw_count, 0u);
    EXPECT_EQ(s.discard_count, 0u);
    EXPECT_EQ(std::memcmp(&s.shuffle_rng, &before, sizeof(RngStream)), 0);
}

// --- Trap 2: shuffles route through the JDK LCG, not xorshift128+ -----------
//
// The resulting draw order is the JdkRandom-seeded Fisher-Yates permutation, NOT
// what the shuffle_rng's own xorshift128+ stream would produce if it drove the
// Fisher-Yates directly. This pins that distinction: the engine result equals the
// JDK-LCG route AND differs from a same-stream xorshift-driven route.
TEST(PilesTrap, ShufflesRouteThroughJdkLcgNotXorshift) {
    CombatState s = make_combat();
    std::vector<CardPoolIndex> discard;
    for (int i = 0; i < 10; ++i) discard.push_back(static_cast<CardPoolIndex>(40 + i));
    set_discard(s, discard);

    const RngStream before = s.shuffle_rng;
    shuffle_discard_into_draw(s);

    // Route A (correct, what the engine does): one random_long -> JDK LCG F-Y.
    const std::vector<CardPoolIndex> jdk_route = expected_reshuffle(before, discard);

    // Route B (the trap's WRONG implementation): Fisher-Yates driven DIRECTLY by
    // the xorshift128+ wrapper on the same starting stream (random(stream, i) is
    // inclusive 0..i, structurally parallel to jdk_shuffle's next_int(i+1)).
    RngStream xs = before;
    std::vector<CardPoolIndex> xs_route = discard;
    for (std::size_t i = xs_route.size() - 1; i > 0; --i) {
        const auto j = static_cast<std::size_t>(random(xs, static_cast<int32_t>(i)));
        std::swap(xs_route[i], xs_route[j]);
    }

    EXPECT_EQ(draw_vec(s), jdk_route);       // engine == JDK-LCG route
    EXPECT_NE(jdk_route, xs_route);          // ... and that route != xorshift route
}

// --- Draw: basic top-first order --------------------------------------------

TEST(PilesDraw, DrawsFromTopOfPile) {
    CombatState s = make_combat();
    set_draw(s, {5, 6, 7});  // 7 is the top (drawn first)
    const int drawn = draw_cards(s, 2);
    EXPECT_EQ(drawn, 2);
    EXPECT_EQ(s.hand_count, 2u);
    EXPECT_EQ(s.hand[0], 7);
    EXPECT_EQ(s.hand[1], 6);
    EXPECT_EQ(s.draw_count, 1u);
    EXPECT_EQ(s.draw[0], 5);
    EXPECT_EQ(s.discard_count, 0u);
}

// --- Hand-size cap (corrected rule: amount = min(amount, 10 - hand)) ---------

TEST(PilesDraw, HandSizeCapOverflowNeverDiscards) {
    CombatState s = make_combat();
    set_hand(s, {0, 1, 2, 3, 4, 5, 6, 7});  // hand_count == 8
    set_draw(s, {20, 21, 22, 23, 24});      // plenty to draw
    // discard deliberately empty: the cap must NOT route any card to discard.

    const int drawn = draw_cards(s, 5);     // request 5, capacity is 10-8 == 2
    EXPECT_EQ(drawn, 2) << "amount capped to 10 - hand_count, not draw-then-discard";
    EXPECT_EQ(s.hand_count, 10u);
    EXPECT_EQ(s.draw_count, 3u);            // exactly 2 removed from the draw pile
    EXPECT_EQ(s.discard_count, 0u);         // capping never touches the discard pile
}

TEST(PilesDraw, FullHandDrawsZero) {
    CombatState s = make_combat();
    set_hand(s, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9});  // hand_count == 10 (full)
    set_draw(s, {20, 21, 22});
    const int drawn = draw_cards(s, 3);
    EXPECT_EQ(drawn, 0);
    EXPECT_EQ(s.hand_count, 10u);
    EXPECT_EQ(s.draw_count, 3u);  // nothing drawn
}

TEST(PilesDraw, NonPositiveAmountDrawsNothing) {
    CombatState s = make_combat();
    set_draw(s, {20, 21, 22});
    EXPECT_EQ(draw_cards(s, 0), 0);
    EXPECT_EQ(draw_cards(s, -3), 0);
    EXPECT_EQ(s.draw_count, 3u);
    EXPECT_EQ(s.hand_count, 0u);
}

// --- Reshuffle mid-draw integration -----------------------------------------

TEST(PilesDraw, ReshuffleMidDrawTriggersExactlyOnce) {
    CombatState s = make_combat();
    set_draw(s, {7});                    // one card left in the deck
    set_discard(s, {1, 2, 3, 4});        // enough in discard to finish the draw
    const RngStream before = s.shuffle_rng;

    const int drawn = draw_cards(s, 3);   // want 3: 1 from draw, reshuffle, 2 more
    EXPECT_EQ(drawn, 3);
    EXPECT_EQ(s.hand_count, 3u);
    EXPECT_EQ(s.hand[0], 7);              // the pre-reshuffle top drew first
    EXPECT_EQ(s.discard_count, 0u);       // discard emptied by the reshuffle
    EXPECT_EQ(s.draw_count, 2u);          // 4 reshuffled in, 2 consumed

    // Exactly one reshuffle => shuffle_rng advanced by exactly 1.
    EXPECT_EQ(s.shuffle_rng.counter, before.counter + 1);

    // The two remaining draw cards are the deepest of the shuffled discard.
    const std::vector<CardPoolIndex> shuffled = expected_reshuffle(before, {1, 2, 3, 4});
    EXPECT_EQ(s.hand[1], shuffled[3]);   // top after reshuffle
    EXPECT_EQ(s.hand[2], shuffled[2]);
    EXPECT_EQ(s.draw[0], shuffled[0]);
    EXPECT_EQ(s.draw[1], shuffled[1]);
}

TEST(PilesDraw, EmptyBothPilesDrawsZeroNoCrash) {
    CombatState s = make_combat();
    const RngStream before = s.shuffle_rng;
    const int drawn = draw_cards(s, 5);
    EXPECT_EQ(drawn, 0);
    EXPECT_EQ(s.hand_count, 0u);
    // Reshuffle was attempted but the discard was empty -> no random_long drawn.
    EXPECT_EQ(std::memcmp(&s.shuffle_rng, &before, sizeof(RngStream)), 0);
}

// Draw the whole deck across a reshuffle boundary: request more than draw+discard
// combined, get exactly (draw+discard) cards, both source piles emptied.
TEST(PilesDraw, DrawExhaustsBothPilesWhenRequestExceedsTotal) {
    CombatState s = make_combat();
    set_draw(s, {7, 8});
    set_discard(s, {1, 2, 3});
    const int32_t shuffle_before = s.shuffle_rng.counter;
    const int drawn = draw_cards(s, 9);  // only 5 exist total (hand cap is 10)
    EXPECT_EQ(drawn, 5);
    EXPECT_EQ(s.hand_count, 5u);
    EXPECT_EQ(s.draw_count, 0u);
    EXPECT_EQ(s.discard_count, 0u);
    EXPECT_EQ(s.shuffle_rng.counter, shuffle_before + 2)
        << "one populated reshuffle plus the recursive empty reshuffle";
}

TEST(PilesDraw, OverdrawWithOnlyDrawCardsStillRunsTheEmptyShuffleAction) {
    CombatState s = make_combat();
    set_draw(s, {7, 8, 9, 10});
    s.relics[0] =
        RelicSlot{static_cast<uint16_t>(RelicId::SUNDIAL), 0};
    s.relic_count = 1;
    const int32_t shuffle_before = s.shuffle_rng.counter;

    EXPECT_EQ(draw_cards(s, 5), 4);
    EXPECT_EQ(s.shuffle_rng.counter, shuffle_before + 1);
    EXPECT_EQ(s.relics[0].counter, 1)
        << "EmptyDeckShuffleAction fires onShuffle even with no discard cards";
}

// --- Exhaust ----------------------------------------------------------------

TEST(PilesExhaust, MovesCardFromHandToExhaustPile) {
    CombatState s = make_combat();
    set_hand(s, {30, 31, 32});
    exhaust_card(s, 31);
    EXPECT_EQ(s.hand_count, 2u);
    EXPECT_EQ(s.hand[0], 30);
    EXPECT_EQ(s.hand[1], 32);       // 32 slid down over the removed slot
    EXPECT_EQ(s.exhaust_count, 1u);
    EXPECT_EQ(s.exhaust[0], 31);
}

TEST(PilesExhaust, NotInHandIsNoOp) {
    CombatState s = make_combat();
    set_hand(s, {30, 31, 32});
    exhaust_card(s, 99);            // not present
    EXPECT_EQ(s.hand_count, 3u);
    EXPECT_EQ(s.exhaust_count, 0u);
}

// --- Energy (GAIN_ENERGY opcode, incl. negative "spend") --------------------
// Spending energy is GAIN_ENERGY with a negative amount (no separate opcode);
// there is no clamp (no max-energy field), so energy can go negative.

TEST(PilesEnergy, GainAndSpendViaGainEnergyNoClamp) {
    CombatState s = make_combat();
    s.player_energy = 3;
    execute_opcode(s, op(Opcode::GAIN_ENERGY, +2));
    EXPECT_EQ(s.player_energy, 5);
    execute_opcode(s, op(Opcode::GAIN_ENERGY, -4));  // spend 4
    EXPECT_EQ(s.player_energy, 1);
    execute_opcode(s, op(Opcode::GAIN_ENERGY, -3));  // overspend -> negative (no clamp)
    EXPECT_EQ(s.player_energy, -2);
}

// --- Dispatch wiring (interp.cpp delegates to piles.cpp) --------------------

TEST(PilesDispatch, DrawOpcodeDrawsThroughInterp) {
    CombatState s = make_combat();
    set_draw(s, {50, 51, 52});
    execute_opcode(s, op(Opcode::DRAW, 2));
    EXPECT_EQ(s.hand_count, 2u);
    EXPECT_EQ(s.hand[0], 52);
    EXPECT_EQ(s.draw_count, 1u);
}

TEST(PilesDispatch, ShuffleInOpcodeReshufflesThroughInterp) {
    CombatState s = make_combat();
    set_discard(s, {60, 61, 62, 63});
    const RngStream before = s.shuffle_rng;
    execute_opcode(s, op(Opcode::SHUFFLE_IN, 0));
    EXPECT_EQ(s.draw_count, 4u);
    EXPECT_EQ(s.discard_count, 0u);
    EXPECT_EQ(s.shuffle_rng.counter, before.counter + 1);
    EXPECT_EQ(draw_vec(s), expected_reshuffle(before, {60, 61, 62, 63}));
}

TEST(PilesDispatch, ExhaustOpcodeExhaustsThroughInterp) {
    CombatState s = make_combat();
    set_hand(s, {70, 71, 72});
    execute_opcode(s, op(Opcode::EXHAUST, 71));  // amount carries the pool index
    EXPECT_EQ(s.hand_count, 2u);
    EXPECT_EQ(s.exhaust_count, 1u);
    EXPECT_EQ(s.exhaust[0], 71);
}

// --- resetAttributes at the PILE-MOVE boundary (S2.43 lead D) ---------------
//
// Soul.update's DRAW_PILE (Soul.java:205-212) and DISCARD_PILE (:213-221) arms
// both call clearPowers() -> AbstractCard.resetAttributes (:2035-2045,
// `costForTurn = cost`) on EVERY move into either pile, mid-turn included --
// not only at the end-turn sweep and on exhaust, which is all the engine used
// to wire.
//
// LIVE WITNESS, STS101166 ps0 floor 20 (s2v2_mindbloom_b capture): Mummified
// Hand set a Bash to costForTurn 0 while it was in hand; the capture shows
// `Bash+(cost 0)` in the discard pile at seq 330 and `Bash+(cost 2)` at seq
// 331, with NO turn boundary between them (the one-record lag is the Soul's
// animation). The card was then reshuffled out of an 8-card discard and drawn
// back by Dark Embrace at cost 2, while the engine still held 0 and played it
// on 0 energy -- the divergence the campaign stopped on.

// A real registry-backed pool row (the pile tests above use bare indices).
CardPoolIndex new_row(CombatState& s, CardId id, uint8_t upgrade = 0) {
    uint8_t slot = 0;
    while (slot < kCardPoolCap &&
           s.card_pool[slot].card_id != static_cast<uint16_t>(CardId::NONE)) {
        ++slot;
    }
    const CardDef* def = card_def(id);
    s.card_pool[slot].card_id = static_cast<uint16_t>(id);
    s.card_pool[slot].upgrade = upgrade;
    s.card_pool[slot].cost_now = card_cost(*def, upgrade);
    s.card_pool[slot].flags = card_flags(*def, upgrade);
    return slot;
}

// MummifiedHand.onUseCard's `c.setCostForTurn(0)` (MummifiedHand.java:67):
// costForTurn only, `cost` untouched -- so the this-turn marker goes on.
void mummified_hand_zero(CombatState& s, CardPoolIndex pi) {
    s.card_pool[pi].cost_now = 0;
    s.card_pool[pi].flags = static_cast<uint16_t>(
        s.card_pool[pi].flags | card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN));
}

TEST(PilesCostReset, MummifiedHandCostZeroIsLostOnTheReshuffle) {
    CombatState s = make_combat();
    const CardPoolIndex bash = new_row(s, CardId::BASH, /*upgrade=*/1);
    mummified_hand_zero(s, bash);
    ASSERT_EQ(s.card_pool[bash].cost_now, 0u);

    set_discard(s, {bash});
    shuffle_discard_into_draw(s);

    EXPECT_EQ(s.draw_count, 1u);
    EXPECT_EQ(s.card_pool[bash].cost_now, 2u)
        << "souls.shuffle lands the card in the DRAW pile -> clearPowers -> "
           "resetAttributes restores costForTurn = cost";
    EXPECT_FALSE(has_card_flag(s.card_pool[bash].flags,
                               CardFlag::COST_MODIFIED_FOR_TURN));
}

// The NEGATIVE control the fix must not break: a COMBAT-PERSISTENT reduction
// moves AbstractCard.cost itself (modifyCostForCombat / updateCost /
// ConfusionPower's `costForTurn = cost = newCost`), carries NO this-turn
// marker, and resetAttributes therefore leaves it exactly where it is.
TEST(PilesCostReset, CombatPersistentCostReductionSurvivesTheSameReshuffle) {
    CombatState s = make_combat();
    const CardPoolIndex bash = new_row(s, CardId::BASH, /*upgrade=*/1);
    s.card_pool[bash].cost_now = 0;  // Corruption-style: cost AND costForTurn
    ASSERT_FALSE(has_card_flag(s.card_pool[bash].flags,
                               CardFlag::COST_MODIFIED_FOR_TURN));

    set_discard(s, {bash});
    shuffle_discard_into_draw(s);

    EXPECT_EQ(s.card_pool[bash].cost_now, 0u)
        << "resetAttributes restores costForTurn from `cost`, which a permanent "
           "writer has already moved -- the reduction must SURVIVE";
}

TEST(PilesCostReset, DeepBreathStyleFullReshuffleResetsToo) {
    CombatState s = make_combat();
    const CardPoolIndex bash = new_row(s, CardId::BASH, /*upgrade=*/1);
    mummified_hand_zero(s, bash);
    set_discard(s, {bash});

    reshuffle_all(s);  // EmptyDeckShuffleAction + ShuffleAction(drawPile)

    EXPECT_EQ(s.draw_count, 1u);
    EXPECT_EQ(s.card_pool[bash].cost_now, 2u);
}

// The other half of the witness: the mid-turn move INTO the discard pile. The
// played card is filed by UseCardAction's `hand.moveToDiscardPile(targetCard)`
// (UseCardAction.java:126), which is the DISCARD_PILE arm.
TEST(PilesCostReset, MidTurnDiscardOfThePlayedCardResetsItsThisTurnCost) {
    CombatState s = make_combat();
    const CardPoolIndex bash = new_row(s, CardId::BASH, /*upgrade=*/1);
    mummified_hand_zero(s, bash);

    limbo_add(s, bash);
    ASSERT_TRUE(file_card_from_limbo(s, bash, /*to_exhaust=*/false,
                                     /*remove_only=*/false));

    EXPECT_EQ(s.discard_count, 1u);
    EXPECT_EQ(s.card_pool[bash].cost_now, 2u);
    EXPECT_FALSE(has_card_flag(s.card_pool[bash].flags,
                               CardFlag::COST_MODIFIED_FOR_TURN));
}

TEST(PilesCostReset, EndOfTurnHandDiscardResetsAsThePileMoveNotOnlyTheSweep) {
    CombatState s = make_combat();
    const CardPoolIndex bash = new_row(s, CardId::BASH, /*upgrade=*/1);
    mummified_hand_zero(s, bash);
    set_hand(s, {bash});

    discard_hand_at_end_of_turn(s);

    EXPECT_EQ(s.discard_count, 1u);
    EXPECT_EQ(s.card_pool[bash].cost_now, 2u);
}

}  // namespace
}  // namespace sts::engine
