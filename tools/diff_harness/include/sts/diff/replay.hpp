#pragma once

// Fight-replay driver: (seed, deck, action[]) -> the sequence of CombatState
// snapshots after each action, produced by combat_begin + repeated advance()
// (the batch API, driven batch-of-1). This is the glue that both the fixture
// generator (to capture expected states) and the reproducer flow (to re-drive a
// saved (seed, action[]) to the same diff) run on top of.
//
// The output convention matches trace.hpp's records: out[0] is the INITIAL
// state from combat_begin (before any action); out[k] (k >= 1) is the state
// AFTER actions[k-1]. So a replay of N actions yields N+1 snapshots, and
// out.back() is the final state.

#include <cstdint>
#include <span>
#include <vector>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::diff {

// The skeleton fight's fixed floor (design doc §9: one combat, no run layer).
// Reproducers/fixtures store only (seed, action[]); the deck and floor are the
// skeleton constants below. Value is arbitrary-but-fixed: floor-scoped RNG
// streams derive as floor_stream(seed, floor), so this must stay stable
// for a reproducer to reproduce.
inline constexpr int32_t kSkeletonFloor = 1;

// The M1 skeleton deck (design doc §9): 5x Strike, 4x Defend, 1x Bash,
// 1x Shrug It Off, 1x Pommel Strike = 12 cards. Shared so fixture generation,
// reproducer replay, and tests all drive the identical deck.
[[nodiscard]] std::vector<engine::CardId> skeleton_deck();

// Drive a fight from combat_begin(seed, floor, deck), applying each action in
// order via advance(). Clears and fills `states_out` with N+1 snapshots (see the
// output convention above).
void replay(int64_t seed, int32_t floor, std::span<const engine::CardId> deck,
            std::span<const engine::Action> actions,
            std::vector<engine::CombatState>& states_out);

// Convenience overload: the skeleton deck at kSkeletonFloor -- the exact fight a
// reproducer's (seed, action[]) refers to.
void replay_skeleton(int64_t seed, std::span<const engine::Action> actions,
                     std::vector<engine::CombatState>& states_out);

}  // namespace sts::diff
