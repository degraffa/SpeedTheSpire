#pragma once

// Per-MonsterId init/turn dispatch + multi-monster spawn (B3.12). Generalizes the
// M1 skeleton's hard-wired single-Jaw-Worm seam: combat_begin now spawns an
// arbitrary monster GROUP, and the pump's step-5 monster-turn hook dispatches to
// each acting monster's own turn function by its id (the "Stage B generalizes the
// MonsterTurnFn per the monster registry" note in advance.hpp/action_queue.hpp).
//
// B3.12 ships JAW_WORM only; the rest of the S1 roster lands in B3.13-B3.22, each
// registering its init/turn here. An unimplemented monster returns nullptr from
// monster_init_fn (spawn_group hard-asserts) and default_monster_turn from
// monster_turn_fn (a live no-op, never reached in-combat pre-B3.13).

#include <cstdint>
#include <span>

#include "sts/engine/action_queue.hpp"  // MonsterTurnFn, default_monster_turn
#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"  // MonsterDef (shared effect-queue helper)

namespace sts::engine {

// Telegraphed intent, generated alongside the monster table (mirrors the alias in
// monster_jaw_worm.hpp; identical type-alias redeclaration is well-formed).
using MonsterIntent = sts::registry::MonsterIntent;

// --- Move-history helpers (AbstractMonster.java:431-491) --------------------
// Shared by the native monster modules. move_history[0] == most recent (Java
// moveHistory.last()); the 0 empty-slot sentinel means `== move` (move != 0)
// already encodes the "history non-empty" checks.

// setMove: record the decided intent and push the move onto the 3-slot ring at
// DECISION time (AbstractMonster.setMove appends to moveHistory).
inline void set_monster_move(MonsterState& m, uint8_t move,
                             MonsterIntent intent) noexcept {
    m.intent = static_cast<uint8_t>(intent);
    m.move_history[2] = m.move_history[1];
    m.move_history[1] = m.move_history[0];
    m.move_history[0] = move;
}

// lastMove(byte): the most-recent decided move == move (:469-474).
inline bool last_move_is(const MonsterState& m, uint8_t move) noexcept {
    return m.move_history[0] == move;
}

// lastTwoMoves(byte): the two most-recent decided moves both == move (:486-491).
inline bool last_two_moves_are(const MonsterState& m, uint8_t move) noexcept {
    return m.move_history[0] == move && m.move_history[1] == move;
}

// The skeleton plays S1's fixed difficulty (Ironclad / Act 1 / A20). Shared by
// every native monster module so their tier columns resolve at one ascension
// until the run layer threads a real ascension through combat_begin. (Mirrors
// monster_jaw_worm.hpp's kSkeletonAscension, kept there for the fixture pin.)
inline constexpr int32_t kMonsterAscension = 20;

// Enqueue a decided move's effect program from the generated MonsterDef, in
// takeTurn addToBottom order, resolving each tier amount at kMonsterAscension --
// the jaw_worm queue_move_effects generalized to any monster/move (design §4.2:
// move effects are data; selection is native). SELF-targeted steps hit `mi`,
// PLAYER steps hit the player; APPLY_POWER's PowerId rides in `flags` (extra).
// (Per-instance amounts -- e.g. the Louse's rolled bite damage -- are NOT in the
// table; the monster module queues those itself, see monster_louse.cpp.)
void queue_monster_move_effects(CombatState& state, uint8_t mi,
                                const sts::registry::MonsterDef& def,
                                uint8_t move) noexcept;

// A monster's spawn-time init: set id, roll HP (monster_hp_rng), do the first
// rollMove (ai_rng) -- the jaw_worm_init shape, generalized. nullptr == the
// monster's module has not landed yet (B3.13+).
using MonsterInitFn = void (*)(CombatState& state, uint8_t monster_index);

// A monster's usePreBattleAction (design §5.2; AbstractMonster.usePreBattleAction),
// run AFTER all ctors+init(), in spawn order (the player's preBattlePrep phase). It
// may draw floor-scoped RNG (e.g. the Louse's curl-up monster_hp_rng roll) and set
// up start-of-combat powers. nullptr == the monster has no pre-battle action.
using MonsterPreBattleFn = void (*)(CombatState& state, uint8_t monster_index);

// The pre-battle function for a monster id, or nullptr if it has none.
[[nodiscard]] MonsterPreBattleFn monster_pre_battle_fn(MonsterId id) noexcept;

// --- B3.17 split framework seams ---------------------------------------------

// A monster's queued-roll body (RollMoveAction.update -> rollMove; no liveness
// check, RollMoveAction.java:17-21). Only monsters whose turns QUEUE ROLL_MOVE
// items (the large slimes: their rolls must resolve after mid-turn interrupt
// SetMoveActions and even posthumously after a split) register one; monsters
// that roll inline in their MonsterTurnFn return nullptr and a ROLL_MOVE item
// targeting them stays a safe no-op.
using MonsterRollMoveFn = void (*)(CombatState& state, uint8_t monster_index);
[[nodiscard]] MonsterRollMoveFn monster_roll_move_fn(MonsterId id) noexcept;

// The ROLL_MOVE opcode body: dispatch to the target's queued-roll function if
// it has one (called by execute_opcode; safe no-op otherwise).
void roll_monster_move(CombatState& state, uint8_t monster_index) noexcept;

// A monster's spawn-at-fixed-HP init: the game's "construct with newHealth,
// then init()" pair (NO monster_hp_rng draw -- the 4-arg slime ctors pass
// newHealth straight through, AbstractMonster.java:139,150 -- then exactly one
// aiRng rollMove). nullptr == the monster cannot be mid-combat spawned yet.
using MonsterSpawnAtHpFn = void (*)(CombatState& state, uint8_t monster_index,
                                    int16_t hp);
[[nodiscard]] MonsterSpawnAtHpFn monster_spawn_at_hp_fn(MonsterId id) noexcept;

// SpawnMonsterAction.update (SpawnMonsterAction.java:42-73) minus presentation:
// insert a fresh record at `slot` (records at >= slot shift up one -- the game
// list-inserts and NEVER removes dead records, MonsterGroup.java:35-40), remap
// monster_queue indices >= slot, ++monster_count (hard assert at kMonsterCap),
// then run the id's spawn-at-hp init (the child's aiRng roll happens HERE, at
// resolve time). Pending action_queue items are NOT remapped -- the Java holds
// object references, so any action queued to resolve after a spawn must
// pre-compute its post-insertion slot (see monster_slime_large.cpp's trailing
// ROLL_MOVE). The insertion slot itself is pre-computed at QUEUE time from the
// smart-positioning drawX rule (monster_slime_large.hpp).
void spawn_monster_at_slot(CombatState& state, uint8_t slot, MonsterId id,
                           int16_t hp) noexcept;

// Post-damage monster hook -- the AbstractMonster.damage() override seam, run
// AFTER a hit fully lands (op_damage / op_lose_hp, ANY damage type: the Java
// override wraps super.damage() unconditionally, and LoseHPAction also routes
// through damage(), LoseHPAction.java:41). Dispatches by monster_id; the large
// slimes' split interrupt is the first consumer. No-op for everyone else.
void on_monster_damaged(CombatState& state, uint8_t monster_index) noexcept;

// The init function for a monster id, or nullptr if not yet implemented.
[[nodiscard]] MonsterInitFn monster_init_fn(MonsterId id) noexcept;

// The turn function for a monster id; default_monster_turn (no-op) if not yet
// implemented.
[[nodiscard]] MonsterTurnFn monster_turn_fn(MonsterId id) noexcept;

// MonsterTurnFn-compatible step-5 hook: dispatch to the acting monster's own turn
// function by monsters[monster_index].monster_id. Pass this to pump() in place of
// the old hard-wired jaw_worm_take_turn.
void dispatch_monster_turn(CombatState& state, uint8_t monster_index) noexcept;

// Spawn `group` into monster slots [0, group.size()) in spawn order: set
// monster_count, then call each monster's init fn in order. The five RNG streams
// are independent, so per-monster init (ctor HP roll + rollMove folded) yields the
// same PER-STREAM sequences as the game's phased "all ctors (monster_hp_rng) then
// all init() (ai_rng)" ordering: monster_hp_rng sees the HP rolls in spawn order,
// ai_rng sees the rollMoves in spawn order. (usePreBattleAction -- a later
// monster_hp_rng phase, e.g. Louse curl-up -- is a separate seam for B3.13; no
// B3.12 monster has one.) An unimplemented monster hard-asserts.
void spawn_group(CombatState& state, std::span<const MonsterId> group) noexcept;

// Run every live monster's usePreBattleAction in spawn order (design §5.2: the
// player's preBattlePrep phase, AFTER spawn_group's ctors+init). This is the
// second monster_hp_rng phase (Louse curl-up rolls) -- separate from the ctor HP
// rolls, but on the same stream, so it extends monster_hp_rng's sequence in spawn
// order. A monster with no pre-battle fn is skipped (no draw). combat_begin calls
// this right after spawn_group.
void use_pre_battle_actions(CombatState& state) noexcept;

}  // namespace sts::engine
