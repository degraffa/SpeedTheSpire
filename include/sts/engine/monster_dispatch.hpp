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

namespace sts::engine {

// A monster's spawn-time init: set id, roll HP (monster_hp_rng), do the first
// rollMove (ai_rng) -- the jaw_worm_init shape, generalized. nullptr == the
// monster's module has not landed yet (B3.13+).
using MonsterInitFn = void (*)(CombatState& state, uint8_t monster_index);

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

}  // namespace sts::engine
