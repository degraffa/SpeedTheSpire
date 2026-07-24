#pragma once

// Slime Boss native AI and split machinery (B3.20).
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled; files read in full):
//   SlimeBoss.java:84-107 (ctor: fixed 140 HP, A9 150; damage table 9/35,
//     A4 10/38; SplitPower; only SLAM damage[1] is used),
//     :120-160 (takeTurn: STICKY creates 3 Slimed in discard, A19 5, then
//     direct PREP_SLAM; PREP_SLAM direct SLAM; SLAM DamageAction then direct
//     STICKY; SPLIT queues CannotLose -> Suicide(this,false) ->
//     Spawn SpikeSlime_L(-385,20,0,currentHealth) ->
//     Spawn AcidSlime_L(120,-8,0,currentHealth) -> CanLose),
//     :172-181 (damage() interrupt at <= half HP, no splitTriggered field),
//     :184-191 (getMove consumes the init roll argument but firstTurn forces
//     STICKY; subsequent moves are all direct setMove calls);
//   MakeTempCardInDiscardAction.java:24-31,41-50 (fresh card copies go to the
//     discard pile; no hand-cap branch);
//   SpawnMonsterAction.java:28-59 (child init at resolve time and smart
//     positioning), SuicideAction.java:21-36, CannotLoseAction.java /
//     CanLoseAction.java:12-15, SetMoveAction.java:52-56,
//     SplitPower.java:19-26, AbstractMonster.java:139,150,431-437,465-467,
//     712-715,869,925-951.
//
// At the fixed S1 A20 difficulty the boss is 150 HP, Slam is 38, and Goop
// Spray creates five Slimed. The registry retains the A4/A9/A19 columns.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Constructor + init(): fixed registry HP (NO monsterHpRng draw), SplitPower,
// one aiRng.random(99) draw whose value is ignored, forced first STICKY move.
void slime_boss_init(CombatState& state, uint8_t monster_index) noexcept;

// Native deterministic takeTurn state machine and split sequence.
void slime_boss_take_turn(CombatState& state,
                          uint8_t monster_index) noexcept;

// SlimeBoss.damage override, called after the hit fully lands.
void slime_boss_on_damaged(CombatState& state,
                           uint8_t monster_index) noexcept;

}  // namespace sts::engine
