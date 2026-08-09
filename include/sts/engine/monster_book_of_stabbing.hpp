#pragma once

// The Book of Stabbing (BookOfStabbing.java, read in full; registry/monsters.yaml
// id 39) -- the solo Act-2 elite whose whole mechanic is a counter that only ever
// goes up.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), all read in full:
//   BookOfStabbing.java:31-158; PainfulStabsPower.java:18-46;
//   MonsterHelper.java:504-506; AbstractPlayer.java:1449-1453 (the hook its
//   power binds); AbstractMonster.java:431-491,705-715,765-775.
//
// FOUR THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) `stabCount` IS WRITTEN BY getMove, NOT BY takeTurn -- the only getMove in
//     the roster with a side effect. The four paths (:129-150) are:
//
//         num < 15 && lastMove(BIG_STAB)  -> ++stabCount; setMove(STAB, stabCount)
//         num < 15                        -> setMove(BIG_STAB); if (A18) ++stabCount
//         lastTwoMoves(STAB)              -> setMove(BIG_STAB); if (A18) ++stabCount
//         else                            -> ++stabCount; setMove(STAB, stabCount)
//
//     so at ascension >= 18 EVERY decision increments, including the init
//     rollMove -- which is why the first STAB the engine ever telegraphs is
//     already TWO hits, not one, at the fixed A20.
//
// (2) THE HIT COUNT IS READ AT takeTurn TIME, AND IT IS THE VALUE getMove
//     COMMITTED. `for (int i = 0; i < this.stabCount; ++i)` (:89) runs when the
//     turn is taken, but the trailing RollMoveAction (:102) -- the only thing
//     that can change stabCount -- is queued BEHIND every DamageAction the loop
//     emits. So the count the player is shown on the intent banner is the count
//     that lands. It is per-move state, not a recomputation.
//
// (3) STORAGE IS `pad0`, AND IT SATURATES. stabCount is a field initialiser 1
//     (:50) with NO upper bound in the Java. MonsterState.pad0 is the
//     monster-type-scoped scratch byte (the Wizard-charge / Louse-bite /
//     Guardian-baseline precedent) and is one byte, so the engine SATURATES at
//     255 rather than wrapping (the Guardian shift-count precedent). Reaching it
//     needs 254 decisions -- i.e. a fight of roughly 254 turns against a 172 HP
//     elite that deals at least 7 damage a turn -- so it is not reachable in
//     play; it is written down because a silent wrap would be a 255-hit turn
//     followed by a 0-hit one, and that is the kind of thing worth naming rather
//     than leaving to luck.
//
// (4) usePreBattleAction SELF-APPLIES PAINFUL STABS AT AMOUNT -1
//     (:78-81). PainfulStabsPower assigns `amount = -1` explicitly
//     (PainfulStabsPower.java:29), unlike Minion which merely never assigns one;
//     both land at -1 and both take op_apply_power's non-stacking path. The
//     power's own body -- one Wound into the discard PER NON-THORNS HIT, through
//     Hook::ON_INFLICT_DAMAGE -- lives in src/engine/powers/power_painful_stabs.
//     A stabCount of 5 therefore makes FIVE Wounds, because the Java queues five
//     separate DamageActions (:89-92).
//
// die() (:152-156) is `super.die()` then an UNSEEDED CardCrawlGame.sound.play, so
// the class gets an explicit nullptr in monster_die_fn rather than an entry; the
// damage() override (:120-127) is the "Hit" spine animation and likewise gets an
// empty case in on_monster_damaged.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// BookOfStabbing.stabCount's field initialiser (:50). The engine keeps it in
// MonsterState.pad0 -- see note (3).
inline constexpr uint8_t kBookOfStabbingStartStabCount = 1;

void book_of_stabbing_init(CombatState& state, uint8_t monster_index) noexcept;
void book_of_stabbing_take_turn(CombatState& state,
                                uint8_t monster_index) noexcept;

// The trailing RollMoveAction (:102) sits OUTSIDE the switch, so both move
// bodies reach it, and getMove reads `num` on its first branch.
void book_of_stabbing_roll_move(CombatState& state,
                                uint8_t monster_index) noexcept;

// usePreBattleAction (:78-81): the self-applied Painful Stabs marker. No RNG.
void book_of_stabbing_use_pre_battle_action(CombatState& state,
                                            uint8_t monster_index) noexcept;

// getMove (:129-150) at an arbitrary ascension, exposed so the tier-2 tests can
// exercise the sub-A18 arms the fixed A20 never takes. MUTATES pad0 -- note (1).
// `num` is the aiRng.random(99) the caller already drew.
void book_of_stabbing_decide_move(CombatState& state, uint8_t monster_index,
                                  int32_t num, int32_t ascension) noexcept;

}  // namespace sts::engine
