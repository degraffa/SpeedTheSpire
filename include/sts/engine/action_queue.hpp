#pragma once

// Action-queue mechanics + the getNextAction pump (design doc §5.1-§5.2).
// Provenance: com.megacrit.cardcrawl.actions.GameActionManager
// (GameActionManager.java, esp. getNextAction lines 185-367). This layer is
// QUEUE ORDERING/MECHANICS only -- it decides *which* item resolves next and
// maintains the four queues (design doc §5.1). It does NOT interpret opcodes or
// resolve card plays: the effect interpreter is A4.1 and real card-play flow is
// A4.3. Until those land, "executing" an action_queue / pre_turn item just means
// consuming (popping) it, and a non-sentinel card_queue item is a documented
// pop-and-discard no-op stub.
//
// -------------------------------------------------------------------------
// TWO source-vs-recipe reconciliations recorded here (design-doc/Java win over
// the ledger's step-6 parenthetical, per the working-agreements precedence
// rule; both are documented in the ledger Log + design doc change log):
//
// (1) `preTurnActions` storage was missing from CombatState -- added by A3.1
//     (see combat_state.hpp's STOP-THE-LINE header note).
//
// (2) monster_attacks_queued reset placement. The decompiled
//     GameActionManager.java in the reference tree sets `monsterAttacksQueued`
//     to `true` (declaration + line 304) but NEVER sets it back to false
//     anywhere in the whole tree (grep-confirmed: 3 occurrences, one file, none
//     `= false`). Taken literally that flag can never re-open step 4, so
//     monsters would be queued at most once and never take a second turn -- not
//     the game's observable behavior, so this decompiled copy is missing the
//     reset. The ledger's A3.1 recipe put the reset in the start-of-turn
//     sequence (step 6), but that leaves the flag false *during the player's
//     turn*, so the first mid-turn pump (a card play under A4.3) would wrongly
//     fire step 4 and run a spurious monster turn. The correct, Java-faithful
//     placement -- and what design doc §5.2 step 6's generic "reset per-turn
//     counters" actually enumerates (cardsPlayedThisTurn, turnHasEnded, ...,
//     NOT monsterAttacksQueued; GameActionManager.java:333,349-351) -- is:
//     clear the flag when the turn *ends* (the end-turn sentinel, step 3),
//     priming step 4 to queue monsters exactly once, and leave it set through
//     the whole next player turn. Start-of-turn (step 6) does NOT touch it,
//     matching the Java. This keeps the invariant "monster_attacks_queued is
//     true throughout the player's turn" so A4.3's card-play pumps never
//     trip step 4.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// --- Reserved sentinels / placeholders --------------------------------------

// End-turn sentinel: a "null-card" card_queue item (design doc §5.1: "a
// null-card item is the end-turn sentinel"; GameActionManager.java:198-199 keys
// off `cardQueue.get(0).card == null`). CardQueueItem.card_index is a
// CardPoolIndex (uint8_t) into the 160-row pool (indices 0..159), so 255 is a
// permanently-out-of-range marker that can never collide with a real pool row.
// It is deliberately distinct from CardId::NONE (== 0): index 0 is a *valid*
// pool slot (that merely happens to hold an empty card), whereas 255 is "not a
// card reference at all". A4.3 constructs the sentinel via make_end_turn_sentinel().
inline constexpr CardPoolIndex kEndTurnSentinel = 255;

// DRAW opcode value for the start-of-turn DrawCardAction the pump queues
// (design doc §5.2 step 6). A4.1 landed the real opcode table (interp.hpp:
// Opcode), which reserves NOP == 0 as a safe no-op for value-init'd/unrecognized
// items and numbers §6's set from 1 -- so the real DRAW is 4, not the A3.1
// placeholder's 3. This mirror constant is kept so action_queue.cpp needn't
// include interp.hpp in its hot path; interp.hpp's value is authoritative and
// action_queue.cpp static_asserts the two agree (kOpcodeDrawCard ==
// (uint16_t)Opcode::DRAW).
inline constexpr uint16_t kOpcodeDrawCard = 4;

// Reserved actor index meaning "the player" in an ActionQueueItem's src/tgt
// (combat_state.hpp: "player is a reserved sentinel, monsters are monster-array
// indices"). 0xFF cannot alias a monster slot (0..4). A4.1 may formalize this.
inline constexpr uint8_t kActorPlayer = 0xFF;

// Player draw count at start of turn (the game's DrawCardAction(gameHandSize);
// gameHandSize is 5 for the Ironclad skeleton, design doc §9). Carried as the
// queued DrawCard item's `amount`; interpreted by A4.1/A4.2, not here.
inline constexpr int32_t kStartOfTurnDrawCount = 5;

// --- Monster-turn extension point (the A3.1<->A3.2 seam) --------------------

// Step 5 of the pump (design doc §5.2) runs one monster's turn. A3.1 owns the
// QUEUE mechanics only and must not know any monster-specific behavior; A3.2
// supplies the real Jaw Worm move-selection/turn logic. The seam is a plain
// function pointer (this engine is data-oriented: no virtual dispatch, no heap)
// invoked as `take_turn(state, monster_index)` for each live monster popped
// from the monster queue. A3.2 passes its real AI function; A3.1's own tests
// pass default_monster_turn (a no-op) or a lightweight probe.
using MonsterTurnFn = void (*)(CombatState& state, uint8_t monster_index);

// The default step-5 hook: the monster does nothing (no AI in A3.1). It exists
// so pump()'s signature has a valid default and A3.1 is fully testable with
// zero monster-AI knowledge.
void default_monster_turn(CombatState& state, uint8_t monster_index) noexcept;

// --- Queue insertion primitives (design doc §5.1) ---------------------------

// Main action ring (`actions`). addToBottom appends
// (GameActionManager.java:96-100); addToTop prepends (lines 139-143). The game
// makes these no-ops outside the COMBAT room phase; the sim has no non-combat
// phase inside CombatState, so they are unconditional here. Overflow is a hard
// assert (design doc §4.1), never a reallocation.
void add_to_bottom(CombatState& state, ActionQueueItem item) noexcept;
void add_to_top(CombatState& state, ActionQueueItem item) noexcept;

// preTurnActions ring. addToTurnStart prepends (GameActionManager.java:145).
void add_to_turn_start(CombatState& state, ActionQueueItem item) noexcept;

// Card queue (`cardQueue`). Normal enqueue appends to the bottom
// (addCardQueueItem(c) -> addCardQueueItem(c, false); GameActionManager.java:
// 110,114-116). Front-of-queue insertion is TRAP 9 (design doc §10 item 9,
// §5.1; GameActionManager.java:102-108): when the queue is non-empty the item
// goes to index **1**, not 0 -- the currently-resolving head card stays at
// index 0 -- and only lands at index 0 when the queue was empty. Overflow is a
// hard assert.
void add_card_to_queue_bottom(CombatState& state, CardQueueItem item) noexcept;
void add_card_to_queue_top(CombatState& state, CardQueueItem item) noexcept;

// End-turn sentinel constructors/predicate (see kEndTurnSentinel).
[[nodiscard]] CardQueueItem make_end_turn_sentinel() noexcept;
[[nodiscard]] bool is_end_turn_sentinel(CardQueueItem item) noexcept;

// --- Low-level ring pops (front) --------------------------------------------
// Exposed for unit tests (drain-order verification) and reused internally by
// the pump. Return false and leave *out untouched when the ring is empty.
bool pop_action_front(CombatState& state, ActionQueueItem& out) noexcept;
bool pop_pre_turn_front(CombatState& state, ActionQueueItem& out) noexcept;

// --- Pump (design doc §5.2 getNextAction priority order) --------------------

// Classification of a single pump step -- what the priority check did this
// iteration. Lets tests assert resolution *ordering* (e.g. every RAN_ACTION
// precedes every RAN_PRE_TURN) without an effect interpreter to observe.
enum class PumpOutcome : uint8_t {
    RAN_ACTION = 0,      // step 1: popped/executed an action_queue item
    RAN_PRE_TURN,        // step 2: popped/executed a pre_turn_actions item
    END_TURN_SENTINEL,   // step 3: head card_queue item was the end-turn sentinel
    RAN_CARD_QUEUE,      // step 3: head was a non-sentinel card (A4.3 stub: discarded)
    QUEUED_MONSTERS,     // step 4: populated monster_queue from live monsters
    RAN_MONSTER,         // step 5: ran one monster's turn via the extension point
    STARTED_TURN,        // step 6: ran the start-of-turn sequence (turn++)
    WAITING_ON_USER,     // step 7: control returns to the player (terminal)
    COMBAT_OVER,         // player dead or all monsters dead (terminal)
};

struct PumpStepResult {
    PumpOutcome outcome;
    // Valid for RAN_ACTION / RAN_PRE_TURN: the item that was consumed.
    ActionQueueItem executed;
    // Valid for RAN_MONSTER: the monster slot whose turn ran.
    uint8_t monster_index;
};

// Run exactly one getNextAction priority step (design doc §5.2). Advances the
// queues by one and returns what happened. WAITING_ON_USER and COMBAT_OVER are
// terminal; every other outcome means "call again to keep resolving".
PumpStepResult pump_step(CombatState& state, MonsterTurnFn take_turn) noexcept;

// Drive the queues to quiescence: repeatedly pump_step until control returns to
// the player (phase WAITING_ON_USER) or combat ends (phase COMBAT_OVER). This
// is the "pump the loop after a player action" half of the engine step function
// (design doc §5.2). `take_turn` is the step-5 monster-turn seam (default:
// no-op); A3.2 passes real Jaw Worm AI.
void pump(CombatState& state,
          MonsterTurnFn take_turn = default_monster_turn) noexcept;

}  // namespace sts::engine
