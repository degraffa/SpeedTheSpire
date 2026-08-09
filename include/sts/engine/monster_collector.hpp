#pragma once

// The Collector (TheCollector.java, read in full; registry/monsters.yaml
// id 43) -- the Act-2 boss encounter "Collector" (encounters.yaml id 39, a
// single EMIT). The second mid-combat summoner in the roster, and the first
// whose summon RECYCLES fixed slots: REVIVE re-fills exactly the torch-head
// positions whose occupant died. Stats and move effects are generated registry
// data; move selection, both spawn moves and the death sweep are native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), all read in full:
//   TheCollector.java:50-243; TorchHead.java:27-85 (the summon's ctor draws
//   and its ctor setMove); SpawnMonsterAction.java:28-73;
//   MonsterGroup.java:108-115; AbstractMonster.java:431-491, :765-779,
//   :925-951.
//
// FIVE THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) enemySlots IS A HashMap<Integer, AbstractMonster> WITH KEYS 1 AND 2, AND
//     ITS ITERATION ORDER IS COMPUTABLE: Integer.hashCode is the value, the
//     default table has 16 buckets, so entrySet() walks key 1 then key 2 --
//     every REVIVE constructs (and therefore draws for) slot 1's replacement
//     before slot 2's. The engine derives the map instead of storing it, note
//     (2).
//
// (2) THE SLOT MAP DERIVES FROM draw_x -- the Gremlin Leader's identifySlot
//     argument, re-run. Slot k's position is spawnX + -185k (spawnX = -100,
//     :72,:128): -285 for key 1, -470 for key 2, and the Collector itself
//     sits at +60, so the three x's are distinct. `enemySlots.get(k)` is the
//     NEWEST TorchHead spawned at x_k; that record `isDying` exactly when NO
//     live record sits at x_k (older occupants are dead too -- a slot is only
//     re-filled when its occupant died, and dead records never revive). So
//       slot k spawned-at-least-once  == any record with draw_x == x_k
//       slot k's occupant is dying    == no LIVE record with draw_x == x_k
//     and isMinionDead() (:205-211) is "some spawned slot has no live
//     occupant". Storing record indices instead was rejected for the Gremlin
//     Leader's reason verbatim: indices shift on every insertion.
//
// (3) initialSpawn AND ultUsed ARE takeTurn-TIME WRITES (:133,:159), NOT
//     decision-time ones -- unlike the Bronze Orb's usedStasis. Both
//     therefore need real storage (kMonsterFlagCollectorInitialSpawn /
//     kMonsterFlagCollectorUltUsed): the roll after turn 1 happens AFTER
//     takeTurn cleared initialSpawn, but nothing structurally prevents a
//     re-roll in between, so consuming it at init would be a model claim the
//     Java does not make. turnsTaken (:71,:176) counts TURNS TAKEN, not
//     decisions -- it too increments in takeTurn, outside the switch -- and
//     lives in pad0 (saturating; only `>= 3` is ever read).
//
// (4) A SPAWN/REVIVE TURN'S STREAM ORDER IS THE AUTOMATON'S SHAPE: each
//     TorchHead ctor runs at QUEUE time inside the loop -- monsterHpRng
//     super-arg (38,40 flat) then tiered setHp, per head, in slot order; the
//     MathUtils.random(-5, 25) y-jitter is UNSEEDED -- and each spawn's ONE
//     ai_rng init roll happens at its RESOLVE, with the Collector's trailing
//     RollMoveAction after them. The Minion applies are SpawnMonsterAction's
//     addToTop-at-resolve (isMinion = true), modelled as the item right
//     behind each spawn (interp.hpp kSpawnApplyMinion, the S2.24 amendment).
//
// (5) BUFF'S STRENGTH WALK INCLUDES THE COLLECTOR ITSELF: every record that is
//     !isDead && !isDying && !isEscaping gets Strength(strAmt), in slot order
//     (:146-149) -- the Healer's one-template native fan-out, with the block
//     landing FIRST (:141-145; the A19 +5 is composed into the row's a19
//     column). The escape test is real in the Java and vacuous here (nothing
//     in this fight escapes); the engine walk tests it anyway because the
//     predicate IS the Java's.
//
// die() (:228-242) is the Bronze Automaton's exactly: presentation,
// super.die(), onBossVictoryLogic, then the addToTop suicide sweep with
// relicTrigger TRUE -- post-super, monster_die_after_fn, and a swept torch
// head runs the full death edge.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// The Collector's `offsetX` (TheCollector.java:85, the ctor's 9th argument).
inline constexpr int16_t kCollectorDrawX = 60;

// The two torch-head slot positions, indexed by enemySlots key - 1:
// spawnX + -185 * k with spawnX = -100 (:72,:128,:165).
inline constexpr int16_t kTorchHeadSlotX[2] = {-285, -470};

void collector_init(CombatState& state, uint8_t monster_index) noexcept;

void collector_take_turn(CombatState& state, uint8_t monster_index) noexcept;

// The trailing RollMoveAction (:177), queued -- on a SPAWN/REVIVE turn it must
// resolve after the spawned heads' init rolls (note (4)).
void collector_roll_move(CombatState& state, uint8_t monster_index) noexcept;

// getMove (:181-203). `num` is the aiRng.random(99) the caller already drew.
void collector_decide_move(CombatState& state, uint8_t monster_index,
                           int32_t num) noexcept;

// isMinionDead (:205-211) via the draw_x derivation of note (2). Exposed for
// the tier-2 tests that drive the REVIVE arm directly.
[[nodiscard]] bool collector_is_minion_dead(const CombatState& state) noexcept;

// die() AFTER super.die() (:233-239): the suicide sweep.
void collector_die_after(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
