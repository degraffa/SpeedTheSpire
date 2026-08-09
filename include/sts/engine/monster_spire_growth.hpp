#pragma once

// The Spire Growth (the solo Act-3 STRONG group "Spire Growth",
// encounters.yaml id 47). Stats and move effects are generated registry data
// (monsters.yaml id 54); move SELECTION is native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   SpireGrowth.java:30-142; ConstrictedPower.java:17-53;
//   AbstractMonster.java:705-715 (init/rollMove), 765-779 (setHp BOTH
//   overloads), 431-491 (moveHistory / lastMove / lastTwoMoves);
//   RollMoveAction.java:17-21.
//
// ITS CLASS ID IS "Serpent", NOT "SpireGrowth" (SpireGrowth.java:32). The
// encounter row already emits "Serpent"; the monsters.yaml `game_id` matches, and
// that string is the whole join.
//
// THE MOVE TREE (getMove, SpireGrowth.java:100-119) IS FIVE ORDERED GATES, of
// which the first is A17-only:
//
//   A17+ && !player.hasPower(Constricted) && !lastMove(CONSTRICT) -> CONSTRICT
//   num < 50 && !lastTwoMoves(QUICK_TACKLE)                       -> QUICK_TACKLE
//   !player.hasPower(Constricted) && !lastMove(CONSTRICT)         -> CONSTRICT
//   !lastTwoMoves(SMASH)                                          -> SMASH
//                                                                 -> QUICK_TACKLE
//
// The A17 arm is the SAME gate as the third, HOISTED ABOVE the `num` branch --
// so the only thing ascension changes here is PRIORITY, not availability. At the
// engine's fixed A20 the hoisted copy is always live, which means an unafflicted
// player eats Constrict at the first opportunity that is not a repeat. The
// unhoisted arm is authored anyway and is reachable only below A17, which is why
// `ascension` is a parameter rather than the kMonsterAscension constant read
// inline (the Snake Plant precedent).
//
// FOUR THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) IT READS A **PLAYER** POWER, WHICH NO PREVIOUS MONSTER AI DID.
//     `!AbstractDungeon.player.hasPower("Constricted")` appears TWICE in one
//     function (:102,110). That is the rule-of-two threshold on its own
//     (conventions section 7), so the presence test is a named local helper
//     rather than an open-coded slot walk repeated twice -- but it stays LOCAL to
//     this module: it is one loop over a 24-slot array, and promoting it to
//     monster_dispatch.hpp before a second MONSTER needs it would be inventing a
//     shared surface for one caller. (find_power in powers/power_native.hpp is
//     the same walk, but that header is the native-POWER plumbing and monster
//     modules do not include it.)
//
// (2) AI-RNG ACCOUNTING. One ai_rng.random(99) at init(), and it is READ -- the
//     `num < 50` gate consults it on the very first call, and with an empty move
//     history the A17 Constrict gate fires first anyway, so at A20 the opening
//     telegraph is CONSTRICT regardless of the roll. Then exactly one
//     ai_rng.random(99) per turn via the trailing RollMoveAction (:97). NO branch
//     spends a second draw: no nested randomBoolean, no recursion.
//
// (3) ONE monster_hp_rng DRAW, AND THE ROW'S FIXED HP DOES NOT MEAN OTHERWISE.
//     The ctor calls setHp(190) / setHp(170) -- the ONE-ARG overload, which is
//     `setHp(hp, hp)` (AbstractMonster.java:777-779) and still calls
//     monsterHpRng.random(min, max) (:765-767). A degenerate range advances the
//     stream. This is the Hexaghost shape, NOT the Spheric Guardian shape; the
//     Transient and the Maw in this same batch are the other kind and skip the
//     draw entirely. Getting this backwards shifts every later HP draw in the
//     floor.
//
// (4) NO die() OVERRIDE, NO damage() CONSEQUENCE, NO SPAWN PATH. damage()
//     (:121-129) is `super.damage(info)` followed by the standard non-THORNS,
//     output > 0 hurt animation, and changeState (:131-141) has one key; both are
//     presentation, so an empty on_monster_damaged case is the complete
//     translation. Nothing splits or summons a Spire Growth.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// getMove's decision (SpireGrowth.java:100-119) as a pure function of the move
// history, the rolled `num`, whether the PLAYER currently holds Constricted, and
// the ASCENSION -- the last two threaded as parameters so the tier-2 tests can
// drive all four corners without building a player power list, and so the
// sub-A17 arm (whose only difference is where the Constrict gate sits) stays
// exercisable. The engine always calls it at kMonsterAscension.
void spire_growth_decide_move(MonsterState& m, int32_t num,
                              bool player_constricted,
                              int32_t ascension) noexcept;

void spire_growth_init(CombatState& state, uint8_t monster_index) noexcept;

// The trailing RollMoveAction (SpireGrowth.java:97), reached by all three cases.
void spire_growth_roll_move(CombatState& state, uint8_t monster_index) noexcept;

void spire_growth_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
