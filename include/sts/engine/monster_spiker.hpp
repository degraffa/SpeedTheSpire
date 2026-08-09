#pragma once

// The Spiker -- the third of the three "ancient shapes" ("3 Shapes" / "4 Shapes"
// / "Sphere and 2 Shapes"). Stats and move effects are generated registry data
// (monsters.yaml id 53); move SELECTION is native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   Spiker.java:26-110; ThornsPower.java (powers.yaml id 16, ALREADY
//   REGISTERED -- no new row); AbstractMonster.java:705-715 (init/rollMove),
//   765-775 (setHp), 431-491 (moveHistory).
//
// THE A17 THORNS ARM STACKS ON TOP OF THE A2 ARM. usePreBattleAction (:72-79) is
//     new ThornsPower(this, ascensionLevel >= 17 ? startingThorns + 3
//                                                : startingThorns)
// and `startingThorns` is ALREADY 4 from A2 (:62-68). So at the engine's
// kMonsterAscension 20 the opening Thorns is 4 + 3 = SEVEN -- not 6 (which is
// what reading the A17 arm as a restated literal would give) and not 4. Every
// other ascension branch in this batch restates its value; this one composes,
// and it is the one number a reader is most likely to get wrong.
//
// THE THORNS RAMP, END TO END. BUFF_THORNS adds 2 (BUFF_AMT, :50) and increments
// `thornsCount`; getMove (:97-110) returns ATTACK unconditionally once
// `thornsCount > 5`, i.e. from the decision AFTER the sixth BUFF. So an A20
// Spiker's Thorns tops out at 7 + 6*2 = 19 and then it attacks forever. Thorns
// reflects PER HIT (ThornsPower.java's onAttacked, dispatched from op_damage), so
// a "4 Shapes" group holding two ramped Spikers punishes multi-hit attacks
// hard -- that interaction is this batch's named test.
//
// WHERE `thornsCount` LIVES, AND WHY IT IS NOT DERIVED. It is a COUNT, and the
// move history is a 3-slot ring: BUFF turns are not contiguous (the `num < 50 &&
// !lastMove(ATTACK)` arm interleaves attacks), so nothing in the ring recovers
// "how many BUFFs have there been". Unlike the Exploder's turnCount, this one
// genuinely needs storage. It takes MonsterState.pad0 -- the Looter/Mugger
// slashCount precedent -- and NOT a MonsterState.flags bit: the Wave-3 grant
// issued this batch no type-scoped bits, pad0 is free on this monster (the
// Spiker has no per-instance construction roll to keep there), and bits are the
// scarcer namespace. It SATURATES at 6, because the only predicate is `> 5` and a
// uint8 could otherwise wrap in a pathologically long fight.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Spiker.startingThorns (Spiker.java:63,67): 4 from A2, 3 below -- then the
// A17 arm (:75) adds 3 ON TOP of that, so these two are not interchangeable with
// the pre-battle amount. Spelled as the two source constants, with the A17
// addend separate, so the composition is visible rather than pre-multiplied.
inline constexpr int32_t kSpikerStartingThornsBase = 3;   // Spiker.java:67
inline constexpr int32_t kSpikerStartingThornsA2 = 4;     // Spiker.java:63
inline constexpr int32_t kSpikerA17ThornsBonus = 3;       // Spiker.java:75

// getMove's permanent-ATTACK latch: `thornsCount > 5` (Spiker.java:98). The
// counter saturates one above it.
inline constexpr uint8_t kSpikerThornsCountLatch = 5;
inline constexpr uint8_t kSpikerThornsCountMax = 6;

// The opening ThornsPower amount at `ascension`, composing the two branches
// exactly as the Java does. 7 at kMonsterAscension 20.
[[nodiscard]] constexpr int32_t spiker_starting_thorns(int32_t ascension) noexcept {
    const int32_t base = ascension >= 2 ? kSpikerStartingThornsA2
                                        : kSpikerStartingThornsBase;
    return ascension >= 17 ? base + kSpikerA17ThornsBonus : base;
}

void spiker_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (Spiker.java:72-79): one ApplyPowerAction of
// ThornsPower(spiker_starting_thorns(ascension)) on itself. No RNG draw.
void spiker_use_pre_battle_action(CombatState& state,
                                  uint8_t monster_index) noexcept;

// The trailing RollMoveAction (Spiker.java:93), reached by both cases. Draws one
// ai_rng value, which getMove reads unless the thornsCount latch has fired.
void spiker_roll_move(CombatState& state, uint8_t monster_index) noexcept;

void spiker_take_turn(CombatState& state, uint8_t monster_index) noexcept;

// The live `thornsCount` for slot `monster_index` (MonsterState.pad0). Exposed
// for tests; the module is its only writer.
[[nodiscard]] uint8_t spiker_thorns_count(const CombatState& state,
                                          uint8_t monster_index) noexcept;

}  // namespace sts::engine
