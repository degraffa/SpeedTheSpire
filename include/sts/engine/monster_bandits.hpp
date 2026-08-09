#pragma once

// The Masked Bandits trio: BanditPointy ("BanditChild"), BanditLeader,
// BanditBear -- the Act-2 "Masked Bandits" EVENT group (encounters.yaml id 41,
// MonsterHelper.java:513-515), reachable only through the MaskedBandits event
// body. Provenance (each file read in full):
//   * BanditPointy.java (103 lines)  -- ctor :42-58, takeTurn :61-67,
//     deathReact :70-74, getMove :97-100
//   * BanditLeader.java (153 lines)  -- ctor :55-79, deathReact :82-86,
//     takeTurn :89-125, getMove :150-152
//   * BanditBear.java   (139 lines)  -- ctor :55-77, takeTurn :80-102,
//     die :127-133, getMove :136-138
//
// Draw accounting, shared by all three: ONE monster_hp_rng draw in the ctor
// (the Pointy's is the DEGENERATE single-arg setHp form, min == max) and ONE
// ai_rng rollMove draw at init whose num every getMove ignores. No takeTurn
// ever queues a RollMoveAction -- each case ends in a queued SetMoveAction --
// so past init the trio never touches ai_rng (the Torch Head / Looter shape).
//
// deathReact -- the RE-POINTED S2.23 obligation, discharged here as a
// VERIFIED NEGATIVE. BanditBear.die() (:127-133) is super.die() plus a
// deathReact() fan-out over every non-dead/dying group member; it is the ONLY
// deathReact caller in the whole decompiled tree, and the only two overrides
// it can reach -- BanditLeader.deathReact (:82-86) and BanditPointy.deathReact
// (:70-74) -- each queue ONE TalkAction behind !isDeadOrEscaped() and nothing
// else. AbstractMonster.deathReact() itself is EMPTY (:912-913). No move
// changes, no escapeNext(), no stream draw: pure presentation, so the Bear
// registers NO MonsterDieFn / MonsterDieAfterFn (explicit nullptr at the
// dispatch, the Taskmaster precedent) and gremlin move 99 stays unreachable
// in every act. Pinned by CityEventsII.BearDeathReactIsPresentationOnly.
//
// The Leader's MOCK turn (:91-104) reads whether a BanditBear in the group
// isDying -- but only to pick between two TalkAction LINES; nothing
// state-visible branches on it, so the engine body queues nothing for MOCK
// beyond the SetMoveAction chain.
//
// Move graphs (all decided at QUEUE time, inside takeTurn):
//   Pointy: 1 -> 1 forever (2-hit attack).
//   Leader: opens MOCK(2); 2 -> 3; 3 -> 1; 1 -> at A17+ keep 1 until
//           lastTwoMoves(1), else 3 (BanditLeader.java:118-123). Below A17
//           the chain is strictly 1 -> 3 -> 1 -> 3...
//   Bear:   opens BEAR_HUG(2); 2 -> 3; 3 -> 1; 1 -> 3 (LUNGE/MAUL alternate).

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

void bandit_pointy_init(CombatState& s, uint8_t mi) noexcept;
void bandit_pointy_take_turn(CombatState& s, uint8_t mi) noexcept;

void bandit_leader_init(CombatState& s, uint8_t mi) noexcept;
void bandit_leader_take_turn(CombatState& s, uint8_t mi) noexcept;

void bandit_bear_init(CombatState& s, uint8_t mi) noexcept;
void bandit_bear_take_turn(CombatState& s, uint8_t mi) noexcept;

}  // namespace sts::engine
