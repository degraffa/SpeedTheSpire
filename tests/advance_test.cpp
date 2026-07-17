// A5.1 acceptance suite: the batch API (advance / legal_actions / combat_begin;
// design doc §7). Per the ledger's acceptance line:
//   * Batch independence: a batch of 128 states with mixed actions advances
//     independently -- each state's post-advance hash equals the hash of the SAME
//     single state run through its own isolated (batch-of-1) advance().
//   * Determinism: the same 128-state batch + actions run twice -> identical
//     per-state hashes.
//   * combat_begin sanity: a freshly-begun combat has sane invariants.
//   * legal_actions sanity: affordability + phase gating.
//
// Why raw hash comparison (no scratch normalization, unlike cards_test): every
// pair compared here shares an identical byte history -- both sides start from
// byte-identical states (combat_begin is deterministic; the copies are memcpy)
// and undergo the identical deterministic advance() mutations. Drained-scratch
// bytes (vacated ring/pile slots) are therefore identical on both sides, so
// hash_state over the raw bytes is a valid equality test. (cards_test needed
// NormalizeScratch only because it compared against an INDEPENDENTLY hand-built
// state with a different byte history.)

#include <cstdint>
#include <span>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"   // kIroncladBaseEnergy, kStartOfTurnDrawCount
#include "sts/engine/advance.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/monster_jaw_worm.hpp"  // kJawWormHpMin/Max, MonsterIntent
#include "sts/engine/state_hash.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

// The M1 skeleton deck (design doc §9): 5x Strike, 4x Defend, 1x Bash,
// 1x Shrug It Off, 1x Pommel Strike = 12 cards.
std::vector<CardId> SkeletonDeck() {
    std::vector<CardId> deck;
    for (int i = 0; i < 5; ++i) deck.push_back(CardId::STRIKE);
    for (int i = 0; i < 4; ++i) deck.push_back(CardId::DEFEND);
    deck.push_back(CardId::BASH);
    deck.push_back(CardId::SHRUG_IT_OFF);
    deck.push_back(CardId::POMMEL_STRIKE);
    return deck;
}

int64_t SeedFor(int i) { return static_cast<int64_t>(0x51EED00D + i * 2654435761LL); }

// Pick a deterministic, VARIED legal action for state `s` (identified by `i`):
// a mix of PLAY_CARDs at different hand indices and END_TURNs, always legal per
// legal_actions so the test is meaningful (not a degenerate all-end-turn batch).
Action PickMixedAction(const CombatState& s, int i) {
    ActionMask mask{};
    legal_actions(s, mask);

    // Collect the legal play slots.
    int plays[kHandCap];
    int n = 0;
    for (int h = 0; h < kHandCap; ++h) {
        if (mask.can_play[h]) plays[n++] = h;
    }

    // Every 4th state ends its turn; the rest play a legal card, rotating which
    // legal slot by i so hand indices vary across the batch.
    if ((i % 4) == 0 && mask.can_end_turn) {
        return make_action(ActionVerb::END_TURN);
    }
    if (n > 0) {
        const int slot = plays[i % n];
        // Single monster -> target slot 0 (ignored by self-target Skills).
        return make_action(ActionVerb::PLAY_CARD, static_cast<uint8_t>(slot), 0, 0);
    }
    if (mask.can_end_turn) return make_action(ActionVerb::END_TURN);
    return make_action(ActionVerb::END_TURN);
}

// --- combat_begin sanity ----------------------------------------------------

TEST(CombatBegin, FreshCombatHasSaneInvariants) {
    const std::vector<CardId> deck = SkeletonDeck();
    const CombatState s = combat_begin(/*run_seed=*/12345, /*floor=*/1,
                                       std::span<const CardId>(deck));

    // Phase / turn / energy after the initial pump drains through turn 1.
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    EXPECT_EQ(s.turn, 1);
    EXPECT_EQ(s.player_energy, kIroncladBaseEnergy);
    EXPECT_EQ(s.player_hp, 80);       // placeholder A20 HP (design doc §11)
    EXPECT_EQ(s.player_max_hp, 80);
    EXPECT_EQ(s.player_block, 0);

    // Every deck card is in draw + hand (nothing discarded/exhausted yet), and
    // the opening hand is exactly gameHandSize (5).
    EXPECT_EQ(s.hand_count, kStartOfTurnDrawCount);
    EXPECT_EQ(s.hand_count + s.draw_count, static_cast<int>(deck.size()));
    EXPECT_EQ(s.discard_count, 0);
    EXPECT_EQ(s.exhaust_count, 0);
    EXPECT_EQ(s.limbo_count, 0);

    // Jaw Worm rolled and telegraphed its forced first move.
    ASSERT_EQ(s.monster_count, 1);
    EXPECT_EQ(s.monsters[0].monster_id, static_cast<uint16_t>(MonsterId::JAW_WORM));
    EXPECT_GE(s.monsters[0].hp, kJawWormHpMin);
    EXPECT_LE(s.monsters[0].hp, kJawWormHpMax);
    EXPECT_EQ(s.monsters[0].hp, s.monsters[0].max_hp);
    EXPECT_EQ(s.monsters[0].move_history[0], kMoveChomp);
    EXPECT_EQ(s.monsters[0].intent, static_cast<uint8_t>(MonsterIntent::ATTACK));

    // The five floor-scoped streams share the run_seed + floor formula, so they
    // start identical EXCEPT shuffle_rng (drawn once by the deck shuffle) and
    // the two the Jaw Worm consumed at init (monster_hp_rng, ai_rng). misc_rng
    // and card_random_rng were never drawn -> counter 0.
    EXPECT_EQ(s.misc_rng.counter, 0);
    EXPECT_EQ(s.card_random_rng.counter, 0);
    EXPECT_EQ(s.shuffle_rng.counter, 1);      // one random_long for the shuffle
    EXPECT_EQ(s.monster_hp_rng.counter, 1);
    EXPECT_EQ(s.ai_rng.counter, 1);
}

TEST(CombatBegin, IsDeterministicForTheSameSeed) {
    const std::vector<CardId> deck = SkeletonDeck();
    const CombatState a = combat_begin(999, 3, std::span<const CardId>(deck));
    const CombatState b = combat_begin(999, 3, std::span<const CardId>(deck));
    EXPECT_EQ(hash_state(a), hash_state(b));

    // A different floor derives different streams -> different shuffle/HP roll,
    // so (with overwhelming probability) a different state.
    const CombatState c = combat_begin(999, 4, std::span<const CardId>(deck));
    EXPECT_NE(hash_state(a), hash_state(c));
}

// --- legal_actions sanity ---------------------------------------------------

TEST(LegalActions, AffordabilityAndPhaseGating) {
    CombatState s{};
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.monster_count = 1;
    s.monsters[0].hp = 40;
    s.monsters[0].max_hp = 40;
    // Hand: slot 0 a cost-1 card, slot 1 a cost-2 card.
    s.card_pool[0].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.card_pool[0].cost_now = 1;
    s.card_pool[1].card_id = static_cast<uint16_t>(CardId::BASH);
    s.card_pool[1].cost_now = 2;
    s.hand[0] = 0;
    s.hand[1] = 1;
    s.hand_count = 2;

    // Energy 1: the cost-1 card is playable, the cost-2 card is not.
    s.player_energy = 1;
    ActionMask m{};
    legal_actions(s, m);
    EXPECT_TRUE(m.can_play[0]);
    EXPECT_FALSE(m.can_play[1]);       // cost 2 > energy 1
    EXPECT_FALSE(m.can_play[2]);       // beyond hand_count
    EXPECT_TRUE(m.can_end_turn);       // always legal while WAITING_ON_USER

    // Energy 2: both are now affordable.
    s.player_energy = 2;
    legal_actions(s, m);
    EXPECT_TRUE(m.can_play[0]);
    EXPECT_TRUE(m.can_play[1]);

    // Not WAITING_ON_USER: nothing is legal (no play, no end-turn).
    s.phase = static_cast<uint8_t>(CombatPhase::RESOLVING);
    legal_actions(s, m);
    EXPECT_FALSE(m.can_play[0]);
    EXPECT_FALSE(m.can_play[1]);
    EXPECT_FALSE(m.can_end_turn);
}

// --- Batch independence -----------------------------------------------------

TEST(AdvanceBatch, MixedActionsAdvanceIndependently) {
    const std::vector<CardId> deck = SkeletonDeck();
    constexpr int N = 128;

    // 128 combats with varying seeds.
    std::vector<CombatState> originals;
    originals.reserve(N);
    for (int i = 0; i < N; ++i) {
        originals.push_back(
            combat_begin(SeedFor(i), /*floor=*/1, std::span<const CardId>(deck)));
    }

    // A mix of actions, chosen from each state's legal set BEFORE advancing.
    std::vector<Action> actions(N);
    bool saw_play = false, saw_end = false;
    for (int i = 0; i < N; ++i) {
        actions[i] = PickMixedAction(originals[i], i);
        const ActionVerb v = action_verb(actions[i]);
        saw_play = saw_play || (v == ActionVerb::PLAY_CARD);
        saw_end = saw_end || (v == ActionVerb::END_TURN);
    }
    // The batch must genuinely be MIXED, not degenerate.
    ASSERT_TRUE(saw_play) << "test batch had no PLAY_CARD actions";
    ASSERT_TRUE(saw_end) << "test batch had no END_TURN actions";

    // Advance all 128 in one batch call.
    std::vector<CombatState> batch = originals;   // copies
    std::vector<StepResult> batch_results(N);
    advance(std::span<CombatState>(batch), std::span<const Action>(actions),
            std::span<StepResult>(batch_results));

    // Each state, run in isolation (batch-of-1) from the SAME original + action,
    // must reach the byte-identical state (proves no cross-state interference).
    for (int i = 0; i < N; ++i) {
        CombatState single = originals[i];
        StepResult single_result{};
        advance(std::span<CombatState>(&single, 1),
                std::span<const Action>(&actions[i], 1),
                std::span<StepResult>(&single_result, 1));
        EXPECT_EQ(hash_state(batch[i]), hash_state(single))
            << "batch entry " << i << " diverged from its single-state reference";
    }
}

// --- Determinism ------------------------------------------------------------

TEST(AdvanceBatch, SameBatchTwiceIsIdentical) {
    const std::vector<CardId> deck = SkeletonDeck();
    constexpr int N = 128;

    std::vector<CombatState> originals;
    originals.reserve(N);
    for (int i = 0; i < N; ++i) {
        originals.push_back(
            combat_begin(SeedFor(i), /*floor=*/2, std::span<const CardId>(deck)));
    }
    std::vector<Action> actions(N);
    for (int i = 0; i < N; ++i) actions[i] = PickMixedAction(originals[i], i);

    std::vector<CombatState> run1 = originals;
    std::vector<CombatState> run2 = originals;
    std::vector<StepResult> r1(N), r2(N);

    advance(std::span<CombatState>(run1), std::span<const Action>(actions),
            std::span<StepResult>(r1));
    advance(std::span<CombatState>(run2), std::span<const Action>(actions),
            std::span<StepResult>(r2));

    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(hash_state(run1[i]), hash_state(run2[i]))
            << "state " << i << " differs between two identical batch runs";
        EXPECT_EQ(r1[i].terminal, r2[i].terminal) << i;
        EXPECT_EQ(r1[i].reward, r2[i].reward) << i;
    }
}

// A multi-step batch: drive the batch several actions deep and confirm batch ==
// single at every entry (independence holds across repeated advance() calls,
// not just one).
TEST(AdvanceBatch, IndependenceHoldsOverMultipleSteps) {
    const std::vector<CardId> deck = SkeletonDeck();
    constexpr int N = 128;
    constexpr int kSteps = 6;

    std::vector<CombatState> batch;
    batch.reserve(N);
    for (int i = 0; i < N; ++i) {
        batch.push_back(
            combat_begin(SeedFor(i + 500), /*floor=*/1, std::span<const CardId>(deck)));
    }
    std::vector<CombatState> singles = batch;  // parallel isolated copies

    std::vector<Action> actions(N);
    std::vector<StepResult> results(N);

    for (int step = 0; step < kSteps; ++step) {
        for (int i = 0; i < N; ++i) actions[i] = PickMixedAction(batch[i], step * 31 + i);

        advance(std::span<CombatState>(batch), std::span<const Action>(actions),
                std::span<StepResult>(results));

        for (int i = 0; i < N; ++i) {
            StepResult sr{};
            advance(std::span<CombatState>(&singles[i], 1),
                    std::span<const Action>(&actions[i], 1),
                    std::span<StepResult>(&sr, 1));
        }
        for (int i = 0; i < N; ++i) {
            ASSERT_EQ(hash_state(batch[i]), hash_state(singles[i]))
                << "step " << step << " entry " << i << " diverged";
        }
    }
}

}  // namespace
}  // namespace sts::engine
