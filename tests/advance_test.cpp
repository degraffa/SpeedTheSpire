// The batch API: advance / legal_actions / combat_begin (design doc §7).
//   * Batch independence: a batch of 128 states with mixed actions advances
//     independently -- each state's post-advance hash equals the hash of the SAME
//     single state run through its own isolated (batch-of-1) advance().
//   * Determinism: the same 128-state batch + actions run twice -> identical
//     per-state hashes.
//   * combat_begin sanity: a freshly-begun combat has sane invariants.
//   * legal_actions sanity: affordability + phase gating.
//   * The mask-accepting advance() overload: byte-for-byte equivalent to the
//     mask-building one, and it rejects exactly the same illegal actions.
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
#include <cstring>    // std::memcmp (whole-StepResult comparison)
#include <iterator>   // std::size
#include <span>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"   // kIroncladBaseEnergy, kStartOfTurnDrawCount
#include "sts/engine/advance.hpp"
#include "sts/engine/cards.hpp"          // card_def / card_cost / card_flags
#include "sts/engine/combat_state.hpp"
#include "sts/engine/index_cast.hpp"     // as_index (signed loop counter -> subscript)
#include "sts/engine/monster_jaw_worm.hpp"  // kJawWormHpMin/Max, MonsterIntent
#include "sts/engine/state_hash.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

// Every subscript below wraps its index in as_index (index_cast.hpp). Each one
// is a counter running 0..N over a container holding exactly N elements, so the
// conversion is exact; the index arithmetic itself stays signed and unchanged.

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
        actions[as_index(i)] = PickMixedAction(originals[as_index(i)], i);
        const ActionVerb v = action_verb(actions[as_index(i)]);
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
        CombatState single = originals[as_index(i)];
        StepResult single_result{};
        advance(std::span<CombatState>(&single, 1),
                std::span<const Action>(&actions[as_index(i)], 1),
                std::span<StepResult>(&single_result, 1));
        EXPECT_EQ(hash_state(batch[as_index(i)]), hash_state(single))
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
    for (int i = 0; i < N; ++i) actions[as_index(i)] = PickMixedAction(originals[as_index(i)], i);

    std::vector<CombatState> run1 = originals;
    std::vector<CombatState> run2 = originals;
    std::vector<StepResult> r1(N), r2(N);

    advance(std::span<CombatState>(run1), std::span<const Action>(actions),
            std::span<StepResult>(r1));
    advance(std::span<CombatState>(run2), std::span<const Action>(actions),
            std::span<StepResult>(r2));

    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(hash_state(run1[as_index(i)]), hash_state(run2[as_index(i)]))
            << "state " << i << " differs between two identical batch runs";
        EXPECT_EQ(r1[as_index(i)].terminal, r2[as_index(i)].terminal) << i;
        EXPECT_EQ(r1[as_index(i)].reward, r2[as_index(i)].reward) << i;
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
        for (int i = 0; i < N; ++i) actions[as_index(i)] = PickMixedAction(batch[as_index(i)], step * 31 + i);

        advance(std::span<CombatState>(batch), std::span<const Action>(actions),
                std::span<StepResult>(results));

        for (int i = 0; i < N; ++i) {
            StepResult sr{};
            advance(std::span<CombatState>(&singles[as_index(i)], 1),
                    std::span<const Action>(&actions[as_index(i)], 1),
                    std::span<StepResult>(&sr, 1));
        }
        for (int i = 0; i < N; ++i) {
            ASSERT_EQ(hash_state(batch[as_index(i)]), hash_state(singles[as_index(i)]))
                << "step " << step << " entry " << i << " diverged";
        }
    }
}

// --- The legality guard ------------------------------------------------------
//
// advance()'s contract, which the CHOOSE case has always stated ("an illegal
// CHOOSE cannot corrupt state") and which now holds for EVERY verb: an action
// that legal_actions() does not report as legal is a no-op that cannot mutate
// state. The regression these tests pin is a real memory-corruption bug, not a
// tidiness rule -- an unguarded END_TURN appended an end-turn sentinel to
// card_queue while pump_step (action_queue.cpp) short-circuits before the
// card-queue step, so nothing ever drained it. kCardQueueCap steps filled the
// array and the next one wrote past its end, into card_queue_count / pad_cardq /
// monster_queue / monster_attacks_queued / the relic mirror
// (combat_state.hpp) -- caught by an assert in Debug, silent in Release.
//
// The two shapes that reach it are BOTH normal batch-API usage:
//   * a terminal state that keeps being stepped (the usual way to keep a batch
//     uniform instead of compacting finished combats), and
//   * a LIVE state with a hand-select screen open, where the pump blocks on the
//     CHOOSE_CARD at the action-queue head and so never drains the card queue
//     either. This one needs no dead monster at all.

// One step of a batch-of-1.
void Step1(CombatState& s, Action a, StepResult& r) {
    advance(std::span<CombatState>(&s, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&r, 1));
}

// Drive a real skeleton fight with LEGAL actions only (greedily play the first
// playable slot, else end the turn) until it goes terminal. `ok` reports whether
// it actually got there inside the step budget.
CombatState PlayToTerminal(int64_t seed, bool& ok) {
    const std::vector<CardId> deck = SkeletonDeck();
    CombatState s = combat_begin(seed, /*floor=*/1, std::span<const CardId>(deck));
    StepResult r{};
    ok = false;
    for (int step = 0; step < 400; ++step) {
        ActionMask m{};
        legal_actions(s, m);
        Action a = make_action(ActionVerb::END_TURN);
        for (int h = 0; h < kHandCap; ++h) {
            if (m.can_play[h]) {
                a = make_action(ActionVerb::PLAY_CARD, static_cast<uint8_t>(h), 0, 0);
                break;
            }
        }
        Step1(s, a, r);
        if (r.terminal) {
            ok = true;
            break;
        }
    }
    return s;
}

// The fields that sit immediately after card_queue[] in CombatState -- the ones a
// one-past-the-end write lands on (combat_state.hpp). Compared as a group so a
// failure names which neighbour moved.
void ExpectNeighboursUnchanged(const CombatState& before, const CombatState& after) {
    EXPECT_EQ(after.card_queue_count, before.card_queue_count);
    EXPECT_EQ(after.pad_cardq, before.pad_cardq);
    EXPECT_EQ(after.monster_queue_count, before.monster_queue_count);
    EXPECT_EQ(after.monster_attacks_queued, before.monster_attacks_queued);
    EXPECT_EQ(after.relic_count, before.relic_count);
    EXPECT_EQ(after.turn_has_ended, before.turn_has_ended);
    EXPECT_EQ(after.phase, before.phase);
}

// 4x the cap: comfortably past the kCardQueueCap-th append that used to overflow.
constexpr int kOverflowSteps = 4 * kCardQueueCap;

TEST(AdvanceGuard, TerminalStateAbsorbsEndTurnsFarPastTheCardQueueCap) {
    bool ok = false;
    CombatState s = PlayToTerminal(SeedFor(7), ok);
    ASSERT_TRUE(ok) << "the scripted fight never reached a terminal state";
    ASSERT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::COMBAT_OVER));
    ASSERT_EQ(s.card_queue_count, 0) << "terminal state should carry no queued cards";

    const CombatState before = s;
    const uint64_t hash_before = hash_state(s);

    StepResult r{};
    for (int i = 0; i < kOverflowSteps; ++i) {
        Step1(s, make_action(ActionVerb::END_TURN), r);
        ASSERT_EQ(s.card_queue_count, 0)
            << "END_TURN queued work on a terminal state at step " << i;
        EXPECT_TRUE(r.terminal) << "step " << i;
    }

    ExpectNeighboursUnchanged(before, s);
    EXPECT_EQ(hash_state(s), hash_before)
        << "a terminal state must be byte-identical after any number of "
           "rejected END_TURNs";
}

TEST(AdvanceGuard, TerminalStateAbsorbsPlayCardsFarPastTheCardQueueCap) {
    // The PLAY_CARD guard is only under test if hand slot 0 is a REAL index --
    // otherwise queue_card_play's own bounds check rejects the action and the
    // test proves nothing. Most fights end with cards still in hand; scan seeds
    // (deterministically) for one that does rather than assume it.
    bool ok = false;
    CombatState s{};
    for (int k = 0; k < 32; ++k) {
        s = PlayToTerminal(SeedFor(13 + k), ok);
        if (ok && s.hand_count > 0) break;
    }
    ASSERT_TRUE(ok) << "the scripted fight never reached a terminal state";
    ASSERT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::COMBAT_OVER));
    ASSERT_GT(s.hand_count, 0) << "test is vacuous with an empty hand";

    const CombatState before = s;
    const uint64_t hash_before = hash_state(s);

    StepResult r{};
    for (int i = 0; i < kOverflowSteps; ++i) {
        Step1(s, make_action(ActionVerb::PLAY_CARD, 0, 0, 0), r);
        ASSERT_EQ(s.card_queue_count, 0)
            << "PLAY_CARD queued work on a terminal state at step " << i;
        EXPECT_TRUE(r.terminal) << "step " << i;
    }

    ExpectNeighboursUnchanged(before, s);
    EXPECT_EQ(hash_state(s), hash_before)
        << "a terminal state must be byte-identical after any number of "
           "rejected PLAY_CARDs";
}

// --- Illegal actions on a LIVE state ----------------------------------------

TEST(AdvanceGuard, IllegalActionsOnALiveStateLeaveTheHashUnchanged) {
    const std::vector<CardId> deck = SkeletonDeck();
    CombatState s = combat_begin(SeedFor(11), /*floor=*/1,
                                 std::span<const CardId>(deck));
    StepResult r{};

    // Spend the turn's energy with legal plays, so every card still in hand is
    // in range but unaffordable -- an illegal action that is NOT caught by
    // queue_card_play's hand-bound check.
    for (;;) {
        ActionMask m{};
        legal_actions(s, m);
        int slot = -1;
        for (int h = 0; h < kHandCap; ++h) {
            if (m.can_play[h]) { slot = h; break; }
        }
        if (slot < 0) break;
        Step1(s, make_action(ActionVerb::PLAY_CARD, static_cast<uint8_t>(slot), 0, 0), r);
        ASSERT_FALSE(r.terminal) << "the fight ended before energy ran out";
    }

    ASSERT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    ASSERT_GT(s.hand_count, 0);
    ActionMask m{};
    legal_actions(s, m);
    ASSERT_FALSE(m.can_play[0]) << "hand slot 0 should be unaffordable by now";
    ASSERT_TRUE(m.can_end_turn) << "END_TURN is still legal here (and stays legal)";

    const CombatState before = s;
    const uint64_t hash_before = hash_state(s);

    const Action illegal[] = {
        make_action(ActionVerb::PLAY_CARD, 0, 0, 0),                    // unaffordable
        make_action(ActionVerb::PLAY_CARD,
                    static_cast<uint8_t>(s.hand_count), 0, 0),          // past hand_count
        make_action(ActionVerb::PLAY_CARD,
                    static_cast<uint8_t>(kHandCap + 3), 0, 0),          // past kHandCap
        make_action(ActionVerb::PLAY_CARD, 0, 250, 0),                  // absurd target
        make_action(ActionVerb::CHOOSE, 0, 0, 0),                       // nothing to choose
        make_action(ActionVerb::USE_POTION, 0, 0, 0),                   // out of scope here
    };
    for (std::size_t k = 0; k < std::size(illegal); ++k) {
        Step1(s, illegal[k], r);
        EXPECT_EQ(hash_state(s), hash_before) << "illegal action " << k << " mutated the state";
    }
    ExpectNeighboursUnchanged(before, s);

    // The state is still LIVE: the legal action it does have still works.
    Step1(s, make_action(ActionVerb::END_TURN), r);
    EXPECT_NE(hash_state(s), hash_before) << "the guard must not reject a LEGAL END_TURN";
}

TEST(AdvanceGuard, PlayingAnEnemyTargetCardAtANonLiveSlotIsANoOp) {
    const std::vector<CardId> deck = SkeletonDeck();
    CombatState s = combat_begin(SeedFor(23), /*floor=*/1,
                                 std::span<const CardId>(deck));
    ASSERT_EQ(s.monster_count, 1);   // skeleton group: only slot 0 exists

    ActionMask m{};
    legal_actions(s, m);
    int slot = -1;
    for (int h = 0; h < kHandCap; ++h) {
        if (m.can_play_target[h][0]) { slot = h; break; }
    }
    ASSERT_GE(slot, 0) << "opening hand held no playable enemy-target card";

    const uint64_t hash_before = hash_state(s);
    StepResult r{};
    for (int t = 1; t < kMonsterCap; ++t) {
        Step1(s, make_action(ActionVerb::PLAY_CARD, static_cast<uint8_t>(slot),
                             static_cast<uint8_t>(t), 0), r);
        EXPECT_EQ(hash_state(s), hash_before) << "empty monster slot " << t;
    }
    Step1(s, make_action(ActionVerb::PLAY_CARD, static_cast<uint8_t>(slot), 200, 0), r);
    EXPECT_EQ(hash_state(s), hash_before) << "out-of-range monster slot";

    // The same card against the LIVE slot 0 still plays -- the guard rejects the
    // bad target, not the card.
    Step1(s, make_action(ActionVerb::PLAY_CARD, static_cast<uint8_t>(slot), 0, 0), r);
    EXPECT_NE(hash_state(s), hash_before);
}

// --- The live (non-terminal) overflow shape: an open hand-select screen -------

// Allocate a pool row for `id` at `upgrade` (cost/flags from the registry).
CardPoolIndex AddCard(CombatState& s, CardId id, uint8_t upgrade = 0) {
    uint8_t pool_slot = 0;
    while (pool_slot < kCardPoolCap &&
           s.card_pool[pool_slot].card_id != static_cast<uint16_t>(CardId::NONE)) {
        ++pool_slot;
    }
    const CardDef* def = card_def(id);
    s.card_pool[pool_slot].card_id = static_cast<uint16_t>(id);
    s.card_pool[pool_slot].upgrade = upgrade;
    s.card_pool[pool_slot].cost_now = card_cost(*def, upgrade);
    s.card_pool[pool_slot].flags = card_flags(*def, upgrade);
    return pool_slot;
}

CardPoolIndex AddToHand(CombatState& s, CardId id, uint8_t upgrade = 0) {
    const CardPoolIndex idx = AddCard(s, id, upgrade);
    s.hand[s.hand_count++] = idx;
    return idx;
}

// A minimal player-turn combat (one Jaw Worm, WAITING_ON_USER, monster-turn gate
// primed so a mid-turn play resolves without a monster turn).
CombatState MakeCombat() {
    CombatState s{};
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.player_energy = 3;
    s.monster_count = 1;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = 50;
    s.monsters[0].max_hp = 50;
    s.monster_attacks_queued = 1;
    s.turn_has_ended = 0;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    return s;
}

TEST(AdvanceGuard, OpenChoiceScreenAbsorbsEndTurnsAndPlaysWithoutQueueingWork) {
    // True Grit+ prompts for ONE hand card to exhaust; with two eligible cards the
    // hand-select screen opens and the pump blocks on the CHOOSE_CARD.
    CombatState s = MakeCombat();
    AddToHand(s, CardId::TRUE_GRIT, /*upgrade=*/1);   // hand slot 0
    AddToHand(s, CardId::STRIKE);                     // hand slot 1
    AddToHand(s, CardId::DEFEND);                     // hand slot 2

    StepResult r{};
    Step1(s, make_action(ActionVerb::PLAY_CARD, 0, 0, 0), r);

    ActionMask m{};
    legal_actions(s, m);
    ASSERT_TRUE(m.choice_pending) << "expected an open hand-select screen";
    // This is the crux: the phase IS WAITING_ON_USER, so a guard written as a
    // phase test would wave these actions through -- yet the mask says no.
    ASSERT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    ASSERT_FALSE(m.can_end_turn);
    ASSERT_FALSE(m.can_play[0]);
    ASSERT_FALSE(r.terminal) << "this state is LIVE -- no dead monster involved";

    const CombatState before = s;
    const uint64_t hash_before = hash_state(s);

    for (int i = 0; i < kOverflowSteps; ++i) {
        Step1(s, make_action(ActionVerb::END_TURN), r);
        ASSERT_EQ(s.card_queue_count, 0)
            << "END_TURN queued a sentinel behind an open choice at step " << i;
        Step1(s, make_action(ActionVerb::PLAY_CARD, 0, 0, 0), r);
        ASSERT_EQ(s.card_queue_count, 0)
            << "PLAY_CARD queued a card behind an open choice at step " << i;
    }
    ExpectNeighboursUnchanged(before, s);
    EXPECT_EQ(hash_state(s), hash_before);

    // An out-of-range CHOOSE is rejected too, and then the real selection still
    // resolves -- the guard blocks the illegal actions, not the prompt.
    Step1(s, make_action(ActionVerb::CHOOSE, static_cast<uint8_t>(kHandCap), 0, 0), r);
    EXPECT_EQ(hash_state(s), hash_before) << "out-of-range CHOOSE slot";

    Step1(s, make_action(ActionVerb::CHOOSE, 0, 0, 0), r);
    ActionMask m2{};
    legal_actions(s, m2);
    EXPECT_FALSE(m2.choice_pending) << "the selection must still resolve normally";
    EXPECT_TRUE(m2.can_end_turn);
    EXPECT_EQ(s.exhaust_count, 1);
}

// --- The mask-accepting advance() overload -----------------------------------
//
// A caller that used legal_actions() to choose its action already holds the mask
// the three-span advance() would rebuild; the four-span overload takes it
// instead. The overload is a throughput option ONLY -- it must not weaken the
// guard, so these tests pin both halves of that: identical results on legal
// actions, and identical refusals on illegal ones.
//
// The contract (advance.hpp) is that the supplied mask matches the state as it
// stands on entry. Debug and ASan builds assert it, which is why no test here
// feeds a deliberately wrong mask: doing so would abort the binary, which is the
// documented behaviour, not a diagnosable failure.

// One step of a batch-of-1 through the mask overload, building the mask the way
// a caller is required to.
void Step1Masked(CombatState& s, Action a, StepResult& r) {
    ActionMask m{};
    legal_actions(s, m);
    advance(std::span<CombatState>(&s, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&r, 1), std::span<const ActionMask>(&m, 1));
}

TEST(AdvanceMaskOverload, IsByteIdenticalToTheMaskBuildingOverloadOverManySteps) {
    const std::vector<CardId> deck = SkeletonDeck();
    constexpr int N = 128;
    constexpr int kSteps = 8;

    std::vector<CombatState> plain;
    plain.reserve(N);
    for (int i = 0; i < N; ++i) {
        plain.push_back(
            combat_begin(SeedFor(i + 900), /*floor=*/1, std::span<const CardId>(deck)));
    }
    std::vector<CombatState> masked = plain;  // byte-identical starting points

    std::vector<Action> actions(N);
    std::vector<ActionMask> masks(N);
    std::vector<StepResult> r_plain(N);
    std::vector<StepResult> r_masked(N);

    for (int step = 0; step < kSteps; ++step) {
        // Actions are chosen from the PLAIN batch and reused verbatim on the
        // masked one; the two batches only stay in lockstep if the overloads
        // agree, which is the property under test.
        for (int i = 0; i < N; ++i) {
            actions[as_index(i)] = PickMixedAction(plain[as_index(i)], step * 17 + i);
            legal_actions(masked[as_index(i)], masks[as_index(i)]);
        }

        advance(std::span<CombatState>(plain), std::span<const Action>(actions),
                std::span<StepResult>(r_plain));
        advance(std::span<CombatState>(masked), std::span<const Action>(actions),
                std::span<StepResult>(r_masked), std::span<const ActionMask>(masks));

        for (int i = 0; i < N; ++i) {
            ASSERT_EQ(hash_state(plain[as_index(i)]), hash_state(masked[as_index(i)]))
                << "step " << step << " entry " << i << ": states diverged";
            ASSERT_EQ(r_plain[as_index(i)].terminal, r_masked[as_index(i)].terminal)
                << "step " << step << " entry " << i << ": terminal flag";
            ASSERT_EQ(r_plain[as_index(i)].reward, r_masked[as_index(i)].reward)
                << "step " << step << " entry " << i << ": reward";
            // The observation is part of the result, so it is compared too --
            // "exactly equivalent" has to mean the whole StepResult.
            ASSERT_EQ(0, std::memcmp(&r_plain[as_index(i)].obs, &r_masked[as_index(i)].obs,
                                     sizeof(ObsBuffer)))
                << "step " << step << " entry " << i << ": observation";
        }
    }
}

TEST(AdvanceMaskOverload, TerminalStateAbsorbsEndTurnsFarPastTheCardQueueCap) {
    bool reached = false;
    CombatState s = PlayToTerminal(0xB0A710D, reached);
    ASSERT_TRUE(reached) << "fight did not go terminal inside the step budget";

    const CombatState before = s;
    const uint64_t hash_before = hash_state(s);

    // The exact shape that used to overflow card_queue, now driven through the
    // overload: the guard must hold when the caller owns the mask, or the
    // overload has quietly bought throughput with the bug the guard removed.
    StepResult r{};
    for (int i = 0; i < kOverflowSteps; ++i) {
        Step1Masked(s, make_action(ActionVerb::END_TURN), r);
        ASSERT_EQ(s.card_queue_count, 0) << "sentinel queued at step " << i;
        ASSERT_TRUE(r.terminal);
    }
    ExpectNeighboursUnchanged(before, s);
    EXPECT_EQ(hash_state(s), hash_before);
}

TEST(AdvanceMaskOverload, OpenChoiceScreenAbsorbsIllegalActionsAndStillResolves) {
    // A LIVE state with a hand-select screen open: phase is WAITING_ON_USER, so
    // only the mask distinguishes legal from illegal here.
    CombatState s = MakeCombat();
    AddToHand(s, CardId::TRUE_GRIT, /*upgrade=*/1);
    AddToHand(s, CardId::STRIKE);
    AddToHand(s, CardId::DEFEND);

    StepResult r{};
    Step1Masked(s, make_action(ActionVerb::PLAY_CARD, 0, 0, 0), r);

    ActionMask m{};
    legal_actions(s, m);
    ASSERT_TRUE(m.choice_pending) << "expected an open hand-select screen";
    ASSERT_FALSE(r.terminal) << "this state is LIVE";

    const CombatState before = s;
    const uint64_t hash_before = hash_state(s);

    for (int i = 0; i < kOverflowSteps; ++i) {
        Step1Masked(s, make_action(ActionVerb::END_TURN), r);
        ASSERT_EQ(s.card_queue_count, 0) << "END_TURN queued behind a choice at " << i;
        Step1Masked(s, make_action(ActionVerb::PLAY_CARD, 0, 0, 0), r);
        ASSERT_EQ(s.card_queue_count, 0) << "PLAY_CARD queued behind a choice at " << i;
    }
    ExpectNeighboursUnchanged(before, s);
    EXPECT_EQ(hash_state(s), hash_before);

    // ...and the legal selection still goes through: the overload blocks the
    // illegal actions, not the prompt.
    Step1Masked(s, make_action(ActionVerb::CHOOSE, 0, 0, 0), r);
    ActionMask m2{};
    legal_actions(s, m2);
    EXPECT_FALSE(m2.choice_pending);
    EXPECT_TRUE(m2.can_end_turn);
    EXPECT_EQ(s.exhaust_count, 1);
}

TEST(AdvanceMaskOverload, HeterogeneousBatchMixesLegalAndIllegalPerEntry) {
    // Batch entries are independent, and the mask is per entry: a rejected
    // action at index i must not disturb index j. Half the batch gets a legal
    // action, half gets an out-of-range hand slot.
    const std::vector<CardId> deck = SkeletonDeck();
    constexpr int N = 64;

    std::vector<CombatState> batch;
    batch.reserve(N);
    for (int i = 0; i < N; ++i) {
        batch.push_back(
            combat_begin(SeedFor(i + 1300), /*floor=*/1, std::span<const CardId>(deck)));
    }
    const std::vector<CombatState> before = batch;

    std::vector<Action> actions(N);
    std::vector<ActionMask> masks(N);
    std::vector<StepResult> results(N);
    for (int i = 0; i < N; ++i) {
        legal_actions(batch[as_index(i)], masks[as_index(i)]);
        actions[as_index(i)] = (i % 2 == 0)
                         ? make_action(ActionVerb::END_TURN)
                         : make_action(ActionVerb::PLAY_CARD,
                                       static_cast<uint8_t>(kHandCap), 0, 0);
    }

    advance(std::span<CombatState>(batch), std::span<const Action>(actions),
            std::span<StepResult>(results), std::span<const ActionMask>(masks));

    for (int i = 0; i < N; ++i) {
        if (i % 2 == 0) {
            EXPECT_NE(hash_state(batch[as_index(i)]), hash_state(before[as_index(i)]))
                << "entry " << i << ": a legal END_TURN did nothing";
        } else {
            EXPECT_EQ(hash_state(batch[as_index(i)]), hash_state(before[as_index(i)]))
                << "entry " << i << ": an out-of-range play mutated the state";
        }
    }
}

}  // namespace
}  // namespace sts::engine
