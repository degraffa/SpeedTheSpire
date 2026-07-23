// The five skeleton cards + the card-play flow (design doc §5.3, §9; §10 trap
// 10).
//
// Structure:
//   * Per-card table tests (tier-2 pattern): construct a minimal CombatState with
//     the card in hand, queue_card_play + pump to full resolution, assert the
//     resulting fields. Every expected value is hand-computed from the design
//     doc §9 / card-registry table, NOT read back from the implementation.
//   * Trap 10: prove the random-target roll happens at DEQUEUE (resolve), never
//     at ENQUEUE (queue_card_play), consuming exactly one card_random_rng draw
//     and excluding dead monsters.
//   * Full-turn integration: a scripted 3-turn Ironclad-vs-Jaw-Worm fight
//     (fixed seed r0) driven through the real pump, whose hand-traced final state
//     is built INDEPENDENTLY and compared by state hash.
//
// Card table (design doc §9, provenance in cards.hpp):
//   Strike        1E Attack  DAMAGE 6
//   Defend        1E Skill   BLOCK 5
//   Bash          2E Attack  DAMAGE 8, then APPLY Vulnerable 2 (same target)
//   Shrug It Off  1E Skill   BLOCK 8, then DRAW 1
//   Pommel Strike 1E Attack  DAMAGE 9, then DRAW 1

#include <array>
#include <cstdint>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_jaw_worm.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/state_hash.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

// --- Per-card test scaffolding ----------------------------------------------

// A minimal combat with `id` (cost `cost`) as the sole hand card (pool index 0),
// one dummy target monster at 50 HP, and the player-turn invariant set so a
// post-play pump resolves the card and returns to WAITING_ON_USER WITHOUT running
// any monster turn (monster_attacks_queued stays true through the player's turn).
CombatState MakeCardTestState(CardId id, uint8_t cost, int16_t energy = 3) {
    CombatState s{};
    s.card_pool[0].card_id = static_cast<uint16_t>(id);
    s.card_pool[0].cost_now = cost;
    s.hand[0] = 0;
    s.hand_count = 1;
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.player_energy = energy;
    s.monster_count = 1;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = 50;
    s.monsters[0].max_hp = 50;
    s.monster_attacks_queued = 1;
    s.turn_has_ended = 0;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    return s;
}

// --- Per-card table tests ---------------------------------------------------

TEST(CardTable, StrikeDealsSixAndDiscards) {
    CombatState s = MakeCardTestState(CardId::STRIKE, 1);

    // Enqueue-then-dequeue timing: queue_card_play must NOT resolve anything.
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    EXPECT_EQ(s.card_queue_count, 1);          // queued, awaiting dequeue
    EXPECT_EQ(s.monsters[0].hp, 50);           // not yet applied
    EXPECT_EQ(s.hand_count, 1);                // card still in hand
    EXPECT_EQ(s.cards_played_this_turn, 0);

    pump(s);

    EXPECT_EQ(s.monsters[0].hp, 50 - 6);       // DAMAGE 6
    EXPECT_EQ(s.monsters[0].block, 0);
    EXPECT_EQ(s.hand_count, 0);                // moved out of hand
    ASSERT_EQ(s.discard_count, 1);
    EXPECT_EQ(s.discard[0], 0);                // ...to discard
    EXPECT_EQ(s.player_energy, 3 - 1);         // cost 1
    EXPECT_EQ(s.cards_played_this_turn, 1);
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
}

TEST(CardTable, DefendGainsFiveBlock) {
    CombatState s = MakeCardTestState(CardId::DEFEND, 1);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    EXPECT_EQ(s.player_block, 5);              // BLOCK 5
    EXPECT_EQ(s.monsters[0].hp, 50);           // no damage
    EXPECT_EQ(s.hand_count, 0);
    ASSERT_EQ(s.discard_count, 1);
    EXPECT_EQ(s.discard[0], 0);
    EXPECT_EQ(s.player_energy, 3 - 1);
    EXPECT_EQ(s.cards_played_this_turn, 1);
}

TEST(CardTable, BashDealsEightAndAppliesVulnerableTwo) {
    CombatState s = MakeCardTestState(CardId::BASH, 2);
    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    EXPECT_EQ(s.monsters[0].hp, 50 - 8);       // DAMAGE 8 (before Vulnerable)
    ASSERT_EQ(s.monsters[0].power_count, 1);
    EXPECT_EQ(s.monsters[0].powers[0].power_id,
              static_cast<uint16_t>(PowerId::VULNERABLE));
    EXPECT_EQ(s.monsters[0].powers[0].amount, 2);  // APPLY Vulnerable 2
    EXPECT_EQ(s.player_energy, 3 - 2);         // cost 2
    EXPECT_EQ(s.cards_played_this_turn, 1);
    ASSERT_EQ(s.discard_count, 1);
    EXPECT_EQ(s.discard[0], 0);
}

TEST(CardTable, BashDamageIsBeforeVulnerableThenStrikeGetsBonus) {
    // Bash's own DAMAGE lands BEFORE its Vulnerable (addToBot order), so Bash
    // itself deals a flat 8; a Strike into the now-Vulnerable target gets x1.5.
    CombatState s = MakeCardTestState(CardId::BASH, 2);
    s.card_pool[1].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.card_pool[1].cost_now = 1;
    s.hand[1] = 1;
    s.hand_count = 2;

    ASSERT_TRUE(queue_card_play(s, 0, 0));  // Bash
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 42);        // 50 - 8
    EXPECT_EQ(s.monsters[0].powers[0].amount, 2);

    ASSERT_TRUE(queue_card_play(s, 0, 0));  // Strike (now at hand index 0)
    pump(s);
    // 6 * 1.5 (Vulnerable) = 9.0 -> floor 9.
    EXPECT_EQ(s.monsters[0].hp, 42 - 9);
}

TEST(CardTable, StrikeIntoVulnerableTargetGetsBonus) {
    CombatState s = MakeCardTestState(CardId::STRIKE, 1);
    s.monsters[0].powers[0].power_id = static_cast<uint16_t>(PowerId::VULNERABLE);
    s.monsters[0].powers[0].amount = 2;
    s.monsters[0].power_count = 1;

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);
    EXPECT_EQ(s.monsters[0].hp, 50 - 9);    // 6 * 1.5 = 9
}

TEST(CardTable, ShrugItOffGainsEightBlockAndDrawsOne) {
    CombatState s = MakeCardTestState(CardId::SHRUG_IT_OFF, 1);
    // One filler card in the draw pile so DRAW 1 has something to pull.
    s.card_pool[1].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.card_pool[1].cost_now = 1;
    s.draw[0] = 1;
    s.draw_count = 1;

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    EXPECT_EQ(s.player_block, 8);           // BLOCK 8
    ASSERT_EQ(s.hand_count, 1);             // DRAW 1 (Shrug left hand, filler entered)
    EXPECT_EQ(s.hand[0], 1);
    EXPECT_EQ(s.draw_count, 0);
    ASSERT_EQ(s.discard_count, 1);
    EXPECT_EQ(s.discard[0], 0);             // Shrug in discard
    EXPECT_EQ(s.player_energy, 3 - 1);
    EXPECT_EQ(s.cards_played_this_turn, 1);
}

TEST(CardTable, PommelStrikeDealsNineAndDrawsOne) {
    CombatState s = MakeCardTestState(CardId::POMMEL_STRIKE, 1);
    s.card_pool[1].card_id = static_cast<uint16_t>(CardId::STRIKE);
    s.card_pool[1].cost_now = 1;
    s.draw[0] = 1;
    s.draw_count = 1;

    ASSERT_TRUE(queue_card_play(s, 0, 0));
    pump(s);

    EXPECT_EQ(s.monsters[0].hp, 50 - 9);    // DAMAGE 9
    ASSERT_EQ(s.hand_count, 1);             // DRAW 1
    EXPECT_EQ(s.hand[0], 1);
    EXPECT_EQ(s.draw_count, 0);
    ASSERT_EQ(s.discard_count, 1);
    EXPECT_EQ(s.discard[0], 0);
    EXPECT_EQ(s.player_energy, 3 - 1);
    EXPECT_EQ(s.cards_played_this_turn, 1);
}

TEST(CardTable, QueueCardPlayRejectsOutOfRangeHandIndex) {
    CombatState s = MakeCardTestState(CardId::STRIKE, 1);
    EXPECT_FALSE(queue_card_play(s, 5, 0));  // hand_count == 1
    EXPECT_EQ(s.card_queue_count, 0);
}

// --- Trap 10: random target rolls at DEQUEUE, not ENQUEUE --------------------

TEST(CardPlayTrap, RandomTargetRollsAtDequeueNotEnqueue) {
    // (a) A normal (fixed-target) card never touches card_random_rng -- not at
    //     enqueue, and not at dequeue either.
    {
        CombatState s = MakeCardTestState(CardId::STRIKE, 1);
        s.card_random_rng = from_seed(123);
        const int32_t before = s.card_random_rng.counter;

        ASSERT_TRUE(queue_card_play(s, 0, 0));
        EXPECT_EQ(s.card_random_rng.counter, before)
            << "enqueue must not roll a random target";

        pump(s);  // resolve the fixed-target Strike
        EXPECT_EQ(s.card_random_rng.counter, before)
            << "a fixed-target card must not roll cardRandomRng at dequeue either";
    }

    // (b) The dequeue-time target resolution (resolve_play_target) for a
    //     random-target card consumes EXACTLY one card_random_rng draw and picks
    //     only among LIVE monsters (dead slots excluded).
    {
        CombatState s{};
        s.monster_count = 3;
        s.monsters[0].hp = 10;   // alive
        s.monsters[1].hp = 0;    // dead (excluded)
        s.monsters[2].hp = 7;    // alive
        s.card_random_rng = from_seed(555);

        CardDef random_card = kStrike;    // synthetic random-target card
        random_card.random_target = true;

        int32_t counter = s.card_random_rng.counter;
        for (int i = 0; i < 12; ++i) {
            const uint8_t tgt = resolve_play_target(s, random_card, /*declared=*/1);
            EXPECT_TRUE(tgt == 0 || tgt == 2)
                << "random target must skip the dead middle monster; got "
                << static_cast<int>(tgt);
            EXPECT_EQ(s.card_random_rng.counter, counter + 1)
                << "each dequeue roll consumes exactly one card_random_rng draw";
            counter = s.card_random_rng.counter;
        }
    }

    // (c) A single live monster is always chosen (roll still consumes one draw).
    {
        CombatState s{};
        s.monster_count = 3;
        s.monsters[0].hp = 0;    // dead
        s.monsters[1].hp = 5;    // the only live one
        s.monsters[2].hp = 0;    // dead
        s.card_random_rng = from_seed(999);

        CardDef random_card = kStrike;
        random_card.random_target = true;
        const int32_t before = s.card_random_rng.counter;

        EXPECT_EQ(resolve_play_target(s, random_card, /*declared=*/0), 1);
        EXPECT_EQ(s.card_random_rng.counter, before + 1);
    }

    // (d) resolve_play_target on a FIXED-target def returns the declared target
    //     verbatim and never draws.
    {
        CombatState s{};
        s.monster_count = 2;
        s.monsters[0].hp = 5;
        s.monsters[1].hp = 5;
        s.card_random_rng = from_seed(42);
        const int32_t before = s.card_random_rng.counter;

        EXPECT_EQ(resolve_play_target(s, kStrike, /*declared=*/1), 1);
        EXPECT_EQ(s.card_random_rng.counter, before);
    }
}

// --- Full-turn integration: scripted 3-turn Jaw Worm fight -------------------
//
// Fixed seed r0 (-5025562857975149833) drives the Jaw Worm deterministically.
// From the golden Jaw Worm fixture (tests/fixtures/jaw_worm_fixture.tsv, seed r0):
//   HP roll = 44; monster_hp_rng end = {6100601359716733802, 2758559893557545682, 1}.
//   Executed moves: turn1 CHOMP(1), turn2 BELLOW(2), turn3 THRASH(3); the turn-4
//   move rolled at the end of turn 3 is THRASH(3). ai_rng end (after the turn-3
//   roll = fixture "turn 3" row) = {15546794197076016033, 8850432053244781472, 4}.
//
// A20 Jaw Worm effects (JawWorm.java, constants in monster_jaw_worm.hpp):
//   CHOMP  -> 12 damage to the player.
//   BELLOW -> +5 Strength to self, +9 block to self.
//   THRASH -> 7 damage to the player, +5 block to self.
//
// The production jaw_worm_take_turn enqueues the move's real damage/block/power
// effects itself, so this integration fight drives the monster with the
// production function directly -- the move sequence, RNG, and effects all come
// from the real engine, and the expected final state below is still hand-traced
// independently.

const int64_t kSeedR0 = -5025562857975149833LL;

// The fixed 23-card pool shared by the actual run and the independent expected
// state (an immutable input; card_pool never mutates during play). Indices:
//   0 Bash, 1 Strike, 2 Defend, 3 Pommel Strike, 4 Shrug It Off, 5..22 Strike.
void SetupPool(CombatState& s) {
    auto set = [&](int idx, CardId id, uint8_t cost) {
        s.card_pool[idx].card_id = static_cast<uint16_t>(id);
        s.card_pool[idx].cost_now = cost;
    };
    set(0, CardId::BASH, 2);
    set(1, CardId::STRIKE, 1);
    set(2, CardId::DEFEND, 1);
    set(3, CardId::POMMEL_STRIKE, 1);
    set(4, CardId::SHRUG_IT_OFF, 1);
    for (int i = 5; i <= 22; ++i) {
        set(i, CardId::STRIKE, 1);
    }
}

// Zero every drained-scratch region: the four queue rings AND the dead tails of
// the piles. The fixed-capacity design advances counts/cursors and leaves the
// vacated slots' bytes in place (a pop/remove/draw decrements the count without
// clearing the slot), so after a fully-resolved turn the state still carries
// scratch bytes: a stale end-turn sentinel in card_queue[0], stale
// ActionQueueItems in the action ring with non-zero head/tail, and -- the one
// this fight actually hits -- the drawn-out card indices lingering in draw[]
// beyond draw_count. None of that is gameplay state (the rules only ever read
// [0, count) of each pile and only [head, tail) of each ring). Normalizing it
// out on BOTH the actual and the (already-clean) expected state lets the content
// hash reflect gameplay only. The caller asserts every queue count is 0 before
// this runs, so no live queue state is discarded; pile live regions [0, count)
// are left untouched and still hashed.
void NormalizeScratch(CombatState& s) {
    for (auto& it : s.action_queue) it = ActionQueueItem{};
    s.action_head = s.action_tail = s.action_count = 0;
    for (auto& it : s.pre_turn_actions) it = ActionQueueItem{};
    s.pre_turn_head = s.pre_turn_tail = s.pre_turn_count = 0;
    for (auto& it : s.card_queue) it = CardQueueItem{};
    s.card_queue_count = 0;
    for (auto& it : s.monster_queue) it = MonsterQueueItem{};
    s.monster_queue_count = 0;
    // Dead pile tails [count, cap).
    for (int i = s.hand_count; i < kHandCap; ++i) s.hand[i] = 0;
    for (int i = s.draw_count; i < kDrawCap; ++i) s.draw[i] = 0;
    for (int i = s.discard_count; i < kDiscardCap; ++i) s.discard[i] = 0;
    for (int i = s.exhaust_count; i < kExhaustCap; ++i) s.exhaust[i] = 0;
    for (int i = s.limbo_count; i < kLimboCap; ++i) s.limbo[i] = 0;
}

TEST(CardIntegration, ScriptedThreeTurnFightReachesExpectedHash) {
    // ---- Build the initial state: start of the player's turn 1 ----
    CombatState s{};
    SetupPool(s);
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    s.turn = 1;
    s.player_hp = 80;
    s.player_max_hp = 80;
    s.player_energy = 3;

    // Hand: Bash, Strike, Defend, Pommel, Shrug (a normal 5-card opening hand).
    s.hand[0] = 0; s.hand[1] = 1; s.hand[2] = 2; s.hand[3] = 3; s.hand[4] = 4;
    s.hand_count = 5;
    // Draw pile: pool indices 5..22 (18 Strikes), draw[draw_count-1] drawn first.
    for (int i = 0; i < 18; ++i) s.draw[i] = static_cast<uint8_t>(5 + i);
    s.draw_count = 18;

    // Jaw Worm (seed r0). init draws monster_hp_rng once + ai_rng once (forced
    // first move CHOMP), leaving both counters at 1.
    s.monster_count = 1;
    s.monster_hp_rng = from_seed(kSeedR0);
    s.ai_rng = from_seed(kSeedR0);
    jaw_worm_init(s, 0);
    ASSERT_EQ(s.monsters[0].hp, 44) << "seed r0 HP roll (A3.2 fixture)";

    // Player-turn invariant the pump expects (monster_attacks_queued true through
    // the player's turn).
    s.monster_attacks_queued = 1;
    s.turn_has_ended = 0;

    // ---- Turn 1: Bash (monster), Strike (monster), end turn ----
    ASSERT_TRUE(queue_card_play(s, 0, 0));    // Bash -> DMG 8 (44->36), Vuln 2
    pump(s, jaw_worm_take_turn);
    ASSERT_TRUE(queue_card_play(s, 0, 0));    // Strike into Vuln -> 6*1.5=9 (36->27)
    pump(s, jaw_worm_take_turn);
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    pump(s, jaw_worm_take_turn);          // monster CHOMP 12 (player 80->68), SoT2

    // ---- Turn 2: two drawn Strikes, then end turn ----
    // (energy refill to 3 already happened for real inside the prior pump()
    // call's start-of-turn sequence -- the unconditional kIroncladBaseEnergy refill)
    ASSERT_TRUE(queue_card_play(s, 0, 0));    // Strike 22 into Vulnerable
    pump(s, jaw_worm_take_turn);
    ASSERT_TRUE(queue_card_play(s, 0, 0));    // Strike 21 into Vulnerable
    pump(s, jaw_worm_take_turn);
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    pump(s, jaw_worm_take_turn);          // monster BELLOW (+5 Str,+9 blk), SoT3

    // ---- Turn 3: a drawn Strike, then end turn ----
    // (energy already refilled to 3 for real by the prior pump()'s start-of-turn)
    ASSERT_TRUE(queue_card_play(s, 0, 0));    // Strike 17
    pump(s, jaw_worm_take_turn);
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    pump(s, jaw_worm_take_turn);          // monster THRASH 7+Str5=12, no block -> hp 68->56, SoT4

    // ---- Field-by-field checks (the hand trace, for debuggability) ----
    EXPECT_EQ(s.turn, 4);
    EXPECT_EQ(s.player_hp, 56);
    EXPECT_EQ(s.player_block, 0);
    // Turn 3 spends down to 2 (refill 3 - Shrug 1), but the same pump() call
    // that ends turn 3 also drains all the way through start-of-turn 4, which
    // unconditionally refills energy again (kIroncladBaseEnergy) -- so the value
    // observed here is turn 4's fresh 3, not turn 3's leftover 2.
    EXPECT_EQ(s.player_energy, 3);
    EXPECT_EQ(s.cards_played_this_turn, 0);   // reset by start-of-turn 4
    EXPECT_EQ(s.hand_count, 5);
    EXPECT_EQ(s.draw_count, 3);
    EXPECT_EQ(s.discard_count, 15);
    EXPECT_EQ(s.monsters[0].hp, 9);
    EXPECT_EQ(s.monsters[0].block, 5);        // Bellow block clears; Thrash adds 5
    ASSERT_EQ(s.monsters[0].power_count, 2);
    EXPECT_EQ(s.monsters[0].powers[0].power_id,
              static_cast<uint16_t>(PowerId::VULNERABLE));
    EXPECT_EQ(s.monsters[0].powers[0].amount, 2);
    EXPECT_EQ(s.monsters[0].powers[1].power_id,
              static_cast<uint16_t>(PowerId::STRENGTH));
    EXPECT_EQ(s.monsters[0].powers[1].amount, 5);

    // Queues fully drained (guards NormalizeScratch -- no live state lost).
    ASSERT_EQ(s.action_count, 0);
    ASSERT_EQ(s.pre_turn_count, 0);
    ASSERT_EQ(s.card_queue_count, 0);
    ASSERT_EQ(s.monster_queue_count, 0);


    // ---- Independently-constructed expected state (hand-traced values) ----
    CombatState exp{};
    SetupPool(exp);
    exp.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    exp.turn = 4;
    exp.player_hp = 56;
    exp.player_max_hp = 80;
    exp.player_block = 0;
    exp.player_energy = 3;  // start-of-turn-4 refill, see the comment above
    exp.cards_played_this_turn = 0;

    // Final hand (bottom->top): [12,11,10,9,8].
    const std::array<uint8_t, 5> hand = {12, 11, 10, 9, 8};
    for (size_t i = 0; i < hand.size(); ++i) exp.hand[i] = hand[i];
    exp.hand_count = 5;
    // Final draw pile: [5,6,7].
    for (int i = 0; i < 3; ++i) exp.draw[i] = static_cast<uint8_t>(5 + i);
    exp.draw_count = 3;
    // Played cards plus each end-of-turn hand sweep, tail-first.
    const std::array<uint8_t, 15> discard = {0, 1, 4, 3, 2, 22, 21, 18, 19, 20, 17, 13, 14, 15, 16};
    for (size_t i = 0; i < discard.size(); ++i) exp.discard[i] = discard[i];
    exp.discard_count = 15;

    exp.monster_count = 1;
    exp.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    exp.monsters[0].hp = 9;
    exp.monsters[0].max_hp = 44;
    exp.monsters[0].block = 5;
    exp.monsters[0].move_history[0] = kMoveThrash;  // turn-4 decided move
    exp.monsters[0].move_history[1] = kMoveThrash;   // turn 3 executed
    exp.monsters[0].move_history[2] = kMoveBellow;   // turn 2 executed
    exp.monsters[0].intent = static_cast<uint8_t>(MonsterIntent::ATTACK_DEFEND);
    exp.monsters[0].power_count = 2;
    exp.monsters[0].powers[0].power_id = static_cast<uint16_t>(PowerId::VULNERABLE);
    exp.monsters[0].powers[0].amount = 2;
    exp.monsters[0].powers[1].power_id = static_cast<uint16_t>(PowerId::STRENGTH);
    exp.monsters[0].powers[1].amount = 5;

    exp.monster_attacks_queued = 1;  // set during the last end-turn's step 4
    exp.turn_has_ended = 0;          // cleared by start-of-turn 4

    // RNG end states from the golden Jaw Worm fixture (independent oracle).
    exp.monster_hp_rng = RngStream{6100601359716733802ull,
                                   2758559893557545682ull, 1, 0};
    exp.ai_rng = RngStream{15546794197076016033ull, 8850432053244781472ull, 4, 0};
    // shuffle_rng / card_random_rng / misc_rng untouched (never drawn) -> zero,
    // matching the actual run (left value-initialized). No reshuffle occurred.

    // ---- Normalize drained scratch on both, then compare by hash ----
    NormalizeScratch(s);
    NormalizeScratch(exp);

    // Sanity: the actual move history matches the fixture-derived expected one.
    EXPECT_EQ(s.monsters[0].move_history[0], exp.monsters[0].move_history[0]);
    EXPECT_EQ(s.monsters[0].move_history[1], exp.monsters[0].move_history[1]);
    EXPECT_EQ(s.monsters[0].move_history[2], exp.monsters[0].move_history[2]);
    EXPECT_EQ(s.ai_rng.s0, exp.ai_rng.s0);
    EXPECT_EQ(s.ai_rng.s1, exp.ai_rng.s1);
    EXPECT_EQ(s.ai_rng.counter, exp.ai_rng.counter);

    EXPECT_EQ(hash_state(s), hash_state(exp))
        << "scripted 3-turn fight did not reach the hand-traced expected state";
}

}  // namespace
}  // namespace sts::engine
