// Action-queue mechanics + the getNextAction pump (design doc §5.1-§5.2, §10
// trap 9). These tests verify QUEUE ORDERING: the top/bottom insertion
// interleave, the pre-turn-vs-action priority, the end-turn sentinel path, and
// the monster-turn extension point. The action items here carry placeholder
// (NOP) opcodes, so the pump's pop step is a no-op -- this isolates the ordering
// mechanics from effect semantics (execute_opcode is exercised elsewhere).

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

// --- Helpers ----------------------------------------------------------------

// An action item carrying only a distinguishing `amount` -- enough to verify
// drain order without an interpreter.
ActionQueueItem act(int32_t amount) noexcept {
    ActionQueueItem i{};
    i.amount = amount;
    return i;
}

// A non-sentinel card item tagged by card_index for identity checks.
CardQueueItem card(CardPoolIndex idx, uint8_t target = 0) noexcept {
    CardQueueItem c{};
    c.card_index = idx;
    c.target = target;
    return c;
}

// A minimal mid-combat state: player alive, one live Jaw Worm, and
// monster_attacks_queued == true (the player-turn invariant), so the pump
// settles at WAITING_ON_USER unless an end-turn sentinel drives a full cycle.
CombatState make_combat(uint8_t monsters = 1) noexcept {
    CombatState s{};
    s.turn = 1;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    s.player_hp = 50;
    s.player_max_hp = 50;
    s.monster_count = monsters;
    for (uint8_t i = 0; i < monsters; ++i) {
        s.monsters[i].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
        s.monsters[i].hp = 40;
        s.monsters[i].max_hp = 40;
    }
    s.monster_attacks_queued = 1;  // player's turn: monsters not yet queued
    s.turn_has_ended = 0;
    return s;
}

bool is_terminal(PumpOutcome o) noexcept {
    return o == PumpOutcome::WAITING_ON_USER || o == PumpOutcome::COMBAT_OVER;
}

// Probe hook for the step-5 monster-turn extension point (function-pointer
// seam). Records invocations via file-scope counters (MonsterTurnFn takes no
// capture) and marks each monster as having acted.
int g_monster_turns = 0;
void probe_monster_turn(CombatState& s, uint8_t mi) noexcept {
    ++g_monster_turns;
    s.monsters[mi].flags |= 0x1u;  // "acted" marker
}

// --- Layout / size-budget guard ---------------------------------------------

TEST(ActionQueueLayout, PreTurnRingPresentWithinBudget) {
    // pre_turn_actions + turn_has_ended fit inside the CombatState size budget.
    static_assert(sizeof(CombatState) <= 4096,
                  "CombatState must stay within its 4 KB budget after the "
                  "preTurnActions gap-fix");
    CombatState s{};
    EXPECT_EQ(s.pre_turn_count, 0);
    EXPECT_EQ(s.pre_turn_head, 0);
    EXPECT_EQ(s.pre_turn_tail, 0);
    EXPECT_EQ(s.turn_has_ended, 0);
}

// --- Ordering: top-vs-bottom interleave -------------------------------------

TEST(ActionQueueOrdering, TopVsBottomInterleaveDrainOrder) {
    CombatState s{};
    // bottom appends, top prepends. Interleave and check the resulting order.
    add_to_bottom(s, act(1));   // [1]
    add_to_bottom(s, act(2));   // [1,2]
    add_to_top(s, act(3));      // [3,1,2]
    add_to_bottom(s, act(4));   // [3,1,2,4]
    add_to_top(s, act(5));      // [5,3,1,2,4]
    ASSERT_EQ(s.action_count, 5);

    std::vector<int32_t> drained;
    ActionQueueItem out{};
    while (pop_action_front(s, out)) {
        drained.push_back(out.amount);
    }
    EXPECT_EQ(drained, (std::vector<int32_t>{5, 3, 1, 2, 4}));
    EXPECT_EQ(s.action_count, 0);
}

// --- Ordering: preTurn after actions (priority steps 1 vs 2) ----------------

TEST(ActionQueueOrdering, PreTurnDrainsOnlyAfterActions) {
    CombatState s = make_combat();
    // Three main-queue actions, two pre-turn actions.
    add_to_bottom(s, act(10));
    add_to_bottom(s, act(11));
    add_to_bottom(s, act(12));
    add_to_turn_start(s, act(90));
    add_to_turn_start(s, act(91));

    std::vector<PumpOutcome> seq;
    for (;;) {
        const PumpStepResult r = pump_step(s, default_monster_turn);
        seq.push_back(r.outcome);
        if (is_terminal(r.outcome)) {
            break;
        }
    }

    // Every action item drains (steps 1) before any pre-turn item (step 2), and
    // with monster_attacks_queued == true / turn_has_ended == false the pump
    // then settles straight to WAITING_ON_USER -- no monster or turn steps.
    ASSERT_EQ(seq, (std::vector<PumpOutcome>{
                       PumpOutcome::RAN_ACTION, PumpOutcome::RAN_ACTION,
                       PumpOutcome::RAN_ACTION, PumpOutcome::RAN_PRE_TURN,
                       PumpOutcome::RAN_PRE_TURN, PumpOutcome::WAITING_ON_USER}));
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
}

// --- Sentinel triggers the end-of-turn path ---------------------------------

TEST(ActionQueueSentinel, EndTurnSentinelSetsTurnHasEnded) {
    CombatState s = make_combat();
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    ASSERT_EQ(s.card_queue_count, 1);
    ASSERT_EQ(s.turn_has_ended, 0);

    // First non-terminal step consumes the sentinel and opens the end-of-turn
    // path: turn_has_ended set, monster_attacks_queued primed to false.
    const PumpStepResult r = pump_step(s, default_monster_turn);
    EXPECT_EQ(r.outcome, PumpOutcome::END_TURN_SENTINEL);
    EXPECT_EQ(s.turn_has_ended, 1);
    EXPECT_EQ(s.monster_attacks_queued, 0);
    EXPECT_EQ(s.card_queue_count, 0);
}

// --- Turn counter increments over a full end-turn -> start-of-turn cycle -----

TEST(ActionQueueSentinel, FullCycleIncrementsTurnAndReturnsToUser) {
    CombatState s = make_combat();
    s.player_block = 9;  // block should decay to 0 on the default path
    add_card_to_queue_bottom(s, make_end_turn_sentinel());

    pump(s);  // default (no-op) monster turn

    EXPECT_EQ(s.turn, 2);  // exactly one increment
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    EXPECT_EQ(s.turn_has_ended, 0);        // cleared by start-of-turn
    EXPECT_EQ(s.monster_attacks_queued, 1);  // re-armed by the queue step
    EXPECT_EQ(s.player_block, 0);          // default block decay
    EXPECT_EQ(s.action_count, 0);          // the queued DrawCard drained
    EXPECT_EQ(s.card_queue_count, 0);
    EXPECT_EQ(s.monster_queue_count, 0);
}

// --- Monster-turn extension point (steps 4/5) -------------------------------

TEST(ActionQueueMonster, PumpCyclesLiveMonstersThroughExtensionPoint) {
    CombatState s = make_combat(/*monsters=*/2);
    add_card_to_queue_bottom(s, make_end_turn_sentinel());

    g_monster_turns = 0;
    pump(s, probe_monster_turn);

    // Both queued monsters ran their turn via the seam, then the start-of-turn
    // sequence completed -- the pump needs no monster-AI knowledge to do this.
    EXPECT_EQ(g_monster_turns, 2);
    EXPECT_EQ(s.monsters[0].flags & 0x1u, 0x1u);
    EXPECT_EQ(s.monsters[1].flags & 0x1u, 0x1u);
    EXPECT_EQ(s.turn, 2);
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    EXPECT_EQ(s.monster_queue_count, 0);
}

// A dead monster slot is skipped by queueMonsters (hp > 0 liveness proxy).
TEST(ActionQueueMonster, DeadMonsterSlotIsNotQueued) {
    CombatState s = make_combat(/*monsters=*/2);
    s.monsters[1].hp = 0;  // slot 1 dead; slot 0 still alive
    add_card_to_queue_bottom(s, make_end_turn_sentinel());

    g_monster_turns = 0;
    pump(s, probe_monster_turn);

    EXPECT_EQ(g_monster_turns, 1);               // only the live monster acted
    EXPECT_EQ(s.monsters[0].flags & 0x1u, 0x1u);
    EXPECT_EQ(s.monsters[1].flags & 0x1u, 0x0u);
    EXPECT_EQ(s.turn, 2);
}

// --- Trap 9: cardQueue front-insertion is index 1 when busy ------------------

TEST(ActionQueueTrap, CardQueueFrontInsertionIsIndexOneWhenBusy) {
    CombatState s{};

    // Empty queue: front-insertion lands at index 0 (it is the only/first item).
    add_card_to_queue_top(s, card(/*idx=*/10, /*target=*/1));
    ASSERT_EQ(s.card_queue_count, 1);
    EXPECT_EQ(s.card_queue[0].card_index, 10);

    // Non-empty queue: a normal bottom append, then a front-insertion. The head
    // (index 0) MUST stay put; the new item goes to index 1, NOT index 0.
    add_card_to_queue_bottom(s, card(/*idx=*/11, /*target=*/2));  // [10, 11]
    add_card_to_queue_top(s, card(/*idx=*/12, /*target=*/3));     // [10, 12, 11]

    ASSERT_EQ(s.card_queue_count, 3);
    EXPECT_EQ(s.card_queue[0].card_index, 10);  // currently-resolving head stays
    EXPECT_EQ(s.card_queue[1].card_index, 12);  // trap 9: inserted at index 1
    EXPECT_EQ(s.card_queue[2].card_index, 11);  // prior tail shifted right

    // A second busy front-insertion again targets index 1, shifting 12 and 11.
    add_card_to_queue_top(s, card(/*idx=*/13, /*target=*/4));     // [10,13,12,11]
    ASSERT_EQ(s.card_queue_count, 4);
    EXPECT_EQ(s.card_queue[0].card_index, 10);
    EXPECT_EQ(s.card_queue[1].card_index, 13);
    EXPECT_EQ(s.card_queue[2].card_index, 12);
    EXPECT_EQ(s.card_queue[3].card_index, 11);
}

}  // namespace
}  // namespace sts::engine
