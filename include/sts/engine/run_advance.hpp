#pragma once

// run_advance.hpp -- the RUN-LEVEL batch API (Stage B, task B4.4). This is the
// layer ABOVE combat: run_begin() builds the Neow-pending initial run state,
// advance()/legal_actions() drive the floor loop (map choice -> room
// entry -> combat -> reward/proceed -> next floor), and next_room_transition()
// implements the floor++ / floor-stream reseed (trap 7). Combat itself is
// delegated to the existing CombatState machinery (advance.hpp): while a
// RunController is in the COMBAT phase, PLAY_CARD / END_TURN / CHOOSE are
// dispatched into its embedded CombatState and USE_POTION is routed through the
// RunState-owned slot inventory into the combat potion interpreter.
//
// WHY A SEPARATE CONTROLLER STRUCT (not RunState). RunState is the SAVE-PARITY
// persistent state (schema-versioned, hashed, traced -- B4.3 sized it at 2184 B
// and its layout is FROZEN). The transient "where am I in the screen/room flow"
// bookkeeping (current map column, run phase, the live combat, the generated
// encounter lists + their consumption cursors) is NOT save state -- the game
// derives it -- so it lives here in RunController, which embeds a RunState by
// value. This keeps RunState's byte layout untouched (no schema bump) while
// giving the run loop the state it needs. RunController is trivially copyable
// (POD) so a heterogeneous batch of them steps with no allocation.
//
// SCOPE / HONEST BOUNDARIES (B4.4 is the run-layer critical path; the room
// CONTENT tasks are downstream). What is LIVE here:
//   * run start RNG order (monsterRng lists, relicRng pool-shuffle draws, mapRng
//     map) -> the Neow-pending stream state.
//   * the floor loop + trap-7 reseed at every floor entry.
//   * monster-room combat entry via the B3.12 encounter framework + fold-back.
//   * combat-reward / proceed / map-choice screens as CHOOSE states, with kill
//     and Smoke Bomb escape distinguished.
//   * potion-slot legality/consumption in combat plus Fruit Juice / Entropic
//     Brew run mutations outside combat.
// What is DEFERRED (routed to an explicit ROOM_UNIMPLEMENTED / documented seam,
// never faked):
//   * Neow blessing options + payouts  -> B4.14 (here: a single "proceed" skip).
//   * combat REWARD ASSEMBLY (gold/potion/card rolls) -> B4.5 (here: the reward
//     screen exists as a CHOOSE-proceed state; it assembles nothing).
//   * relic pools + acquisition are live through B4.6; downstream reward/chest/
//     shop/Neow tasks decide when to draw/acquire them.
//   * events / shops / rest sites / treasure rooms -> B4.10 / B4.8 / B4.9 / B4.7
//     (here: entering one reseeds the floor streams then parks at
//     ROOM_UNIMPLEMENTED with the stalling RoomType recorded).
//   * A20 run-setup HP/curse modifiers (A6/A10/A14) -> B4.15 (here: base
//     Ironclad sheet). A11 potion slots are live because B4.3 explicitly handed
//     its populated field to this task.
//   * monsters beyond Jaw Worm/Cultist/louses -> B3.14-B3.22 (here: an encounter
//     whose members
//     are not all implemented resolves its composition (miscRng, as the game
//     does) then parks at ROOM_UNIMPLEMENTED rather than asserting in spawn_group).
//
// Provenance (read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * AbstractDungeon.nextRoomTransition  (AbstractDungeon.java:1687-1813): the
//     floorNum++ (1741) THEN the 5-stream reseed (1747-1751, trap 7),
//     monsterList/eliteList.remove(0) on room EXIT (1694-1707), onPlayerEntry
//     (1800).
//   * AbstractDungeon.initializeRelicList (AbstractDungeon.java:1221-1256): 5
//     unconditional relicRng.randomLong() pool-shuffle draws.
//   * Exordium ctor / generateMonsters / initializeBoss (Exordium.java:36-221):
//     the run-start monsterRng draw order (via generate_monster_lists, B3.12).
//   * MonsterRoom.onPlayerEntry (MonsterRoom.java:53-61): getMonsterForRoomCreation
//     -> getEncounter(monsterList.get(0)) -> monsters.init().
//   * AbstractRoom.update battle-over (:277-357): the lifecycle transition points
//     (COMPLETE -> reward screen); the reward ASSEMBLY is B4.5.

#include <cstdint>
#include <span>
#include <type_traits>

#include "sts/engine/advance.hpp"       // ActionMask, StepResult, combat advance/legal_actions
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"    // MonsterLists
#include "sts/engine/map_rooms.hpp"     // RoomType
#include "sts/engine/run_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// --- RunPhase ---------------------------------------------------------------

// The run-level state machine. One enum, all run phases (the run-layer analogue
// of CombatPhase). The value-init default is NONE.
enum class RunPhase : uint8_t {
    NONE = 0,
    NEOW = 1,              // floor 0, Neow blessing pending (content: B4.14).
    MAP_CHOICE = 2,        // choosing the next map node (an outgoing edge).
    COMBAT = 3,            // inside a combat; delegates to the embedded CombatState.
    COMBAT_REWARD = 4,     // post-combat reward/proceed screen (assembly: B4.5).
    ROOM_UNIMPLEMENTED = 5,// entered a room kind / encounter not yet implemented.
    RUN_OVER = 6,          // player dead (loss) -- terminal.
};

// Why combat ended. AbstractRoom.update opens the ordinary reward screen after
// a kill and openCombat(..., true) after Smoke Bomb; both are CHOOSE/proceed
// states but the latter exposes no claimable rewards. DEFEAT transitions to
// RUN_OVER instead.
enum class RunCombatOutcome : uint8_t {
    NONE = 0,
    KILLED = 1,
    SMOKE_BOMB = 2,
    DEFEAT = 3,
};

// CHOOSE arg0 sentinel for "take the boss edge" at a MAP_CHOICE (the boss node is
// not a grid column, so it needs a value outside 0..kMapCols-1).
inline constexpr uint8_t kChooseBoss = static_cast<uint8_t>(kMapCols);  // 7

// cur_x sentinel: the controller is at Neow (floor 0), with no grid column yet.
inline constexpr uint8_t kNeowColumn = 0xFF;

// --- RunController -----------------------------------------------------------

// The whole run-loop state: a RunState (persistent) + a CombatState (the live
// combat / the canonical floor-stream holder) + the transient screen-flow
// bookkeeping. Trivially copyable so a batch of runs steps with no allocation.
struct RunController {
    RunState run;           // persistent save-parity state (map/deck/relics/streams).
    CombatState combat;     // live combat (COMBAT/COMBAT_REWARD); also holds the
                            // reseeded floor streams for every room (design §3.4).

    uint8_t phase;          // RunPhase.
    uint8_t cur_x;          // current map column 0..kMapCols-1; kNeowColumn at Neow.
    uint8_t room_type;      // current RoomType (also identifies an unimplemented stall).
    uint8_t combat_outcome; // RunCombatOutcome; meaningful on reward/run-over.

    // The run's generated encounter-key lists (monsterRng, B3.12) + per-list
    // consumption cursors. A monster room uses monster_list[monster_cursor], an
    // elite uses elite_list[elite_cursor], the boss uses boss_list[boss_cursor];
    // the cursor advances when the room is LEFT (nextRoomTransition remove(0)).
    MonsterLists lists;
    uint8_t monster_cursor;
    uint8_t elite_cursor;
    uint8_t boss_cursor;
    uint8_t pad1;           // explicit padding.
};

static_assert(std::is_trivially_copyable_v<RunController>,
              "RunController must be trivially copyable (POD batch entry)");

// --- RunActionMask -----------------------------------------------------------

// The run-level legal-action set (the run analogue of ActionMask). Which fields
// are meaningful depends on `phase`:
//   NEOW / COMBAT_REWARD : can_proceed (CHOOSE, arg0 ignored).
//   MAP_CHOICE           : can_choose_node[x] over next-row columns + can_choose_boss.
//                          The CHOOSE action's arg0 is the destination column
//                          (0..kMapCols-1) or kChooseBoss.
//   COMBAT               : `combat` holds PLAY_CARD / END_TURN / CHOOSE;
//                          run-owned potion masks below hold USE_POTION.
//   ROOM_UNIMPLEMENTED / RUN_OVER : nothing legal (the run is parked/terminal).
struct RunActionMask {
    uint8_t phase;                     // RunPhase echo (== controller.phase).
    bool can_proceed;                  // NEOW / COMBAT_REWARD single proceed.
    bool can_choose_node[kMapCols];    // MAP_CHOICE: legal next-node columns.
    bool can_choose_boss;              // MAP_CHOICE: the boss edge is available.
    // USE_POTION owns a RunState slot, so its legality lives at this layer.
    // Fruit Juice and Entropic Brew can be used in stable non-combat phases.
    // During COMBAT, can_use_potion_target[slot][monster] enumerates target-
    // required potions; non-target potions use can_use_potion[slot] directly.
    bool can_use_potion[kPotionCap];
    bool can_use_potion_target[kPotionCap][kMonsterCap];
    ActionMask combat;                 // COMBAT: delegated combat legal actions.
};

static_assert(std::is_trivially_copyable_v<RunActionMask>);

// --- API ---------------------------------------------------------------------

// Build the Neow-pending initial run state for (seed, ascension). Reproduces the
// dungeon-init RNG order so the run streams are at their post-init counters
// before Neow: monsterRng advanced by the encounter-list generation, relicRng by
// the 5 pool-shuffle draws, mapRng at end-of-generateMap (the full act map is
// written into run.map). The returned controller is at phase == NEOW.
//
// Character sheet is the BASE Ironclad sheet (80/80 HP, 99 gold, 5 Strike / 4
// Defend / 1 Bash); potion slots apply A11. The remaining A20 run-setup
// modifiers (A6/A10/A14) are B4.15's documented seam.
[[nodiscard]] RunController run_begin(int64_t seed, uint8_t ascension) noexcept;

// Fill `out` with the current run-level legal actions for `rc` (see RunActionMask).
// This overload completes the one-name/all-phases API promised by stage-a §7.
void legal_actions(const RunController& rc, RunActionMask& out) noexcept;

// Step a heterogeneous batch of runs by one action each (the run-level analogue
// of advance()). Each index is dispatched INDEPENDENTLY by its own phase: a
// COMBAT entry pumps its embedded CombatState (and folds back on combat end); a
// MAP_CHOICE consumes a CHOOSE(column); a NEOW / COMBAT_REWARD consumes a
// CHOOSE(proceed). No heap allocation in the loop. results[i] carries the
// terminal flag, the combat reward passthrough, and (in COMBAT) the combat
// observation (zeroed otherwise).
void advance(std::span<RunController> runs, std::span<const Action> actions,
             std::span<StepResult> results) noexcept;

// Compatibility spellings for the in-progress B4.4 branch. They are thin
// wrappers over the public overloads above, not a second implementation.
inline void run_legal_actions(const RunController& rc,
                              RunActionMask& out) noexcept {
    legal_actions(rc, out);
}
inline void run_advance(std::span<RunController> runs,
                        std::span<const Action> actions,
                        std::span<StepResult> results) noexcept {
    advance(runs, actions, results);
}

// --- Exposed helpers (also unit-tested directly) -----------------------------

// The floor transition: advance the leaving-room cursor, ++run.floor, reseed the
// 5 floor-scoped streams to floor_stream(seed, floor) (trap 7: AFTER the
// increment), move to the destination node, and run its onPlayerEntry. `dst_x`
// is the destination column (ignored when `to_boss`); `to_boss` takes the boss
// edge. Public for the trap-7 named test.
void next_room_transition(RunController& rc, uint8_t dst_x, bool to_boss) noexcept;

// The current grid row (floor-1) for a controller on the map, or -1 at Neow.
[[nodiscard]] constexpr int run_cur_row(const RunController& rc) noexcept {
    return rc.run.floor == 0 ? -1 : static_cast<int>(rc.run.floor) - 1;
}

}  // namespace sts::engine
