#pragma once

// The Taskmaster (Taskmaster.java, read in full; registry/monsters.yaml id 38) --
// the ELITE of the Act-2 "Slavers" group, and the Colosseum's "Colosseum Nobs"
// partner. Its Java class name is Taskmaster; its `ID`, its oracle join key and
// the string every encounter emits are all "SlaverBoss" (Taskmaster.java:33).
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), all read in full:
//   Taskmaster.java:31-110; MonsterHelper.java:510-512 ("Slavers"), :516-518
//   ("Colosseum Nobs"); AbstractMonster.java:705-715,765-775.
//
// THREE THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) THE CONSTRUCTOR DRAWS monster_hp_rng TWICE, and no other monster in the
//     roster does. The `super(...)` call's third argument is literally
//     `AbstractDungeon.monsterHpRng.random(54, 60)` (:50), which Java evaluates
//     BEFORE the constructor body runs; the body then calls setHp(57,64) at
//     ascension >= 8 (:52-56), which is a second draw (AbstractMonster.java:
//     765-775). The first value is immediately overwritten and is invisible in
//     the resulting HP -- but it MOVED THE STREAM, and the Taskmaster sits at
//     group index 1 of "Slavers", so missing it shifts the Red Slaver's HP and
//     everything after it on that floor.
//
//     It is registry data, not a hand-written pre-draw: the row's `rolls:` entry
//     SUPER_ARG_HP carries timing CONSTRUCTOR_BEFORE_HP, which is also what makes
//     burn_unspawned_ctor_rolls order it correctly if a Taskmaster is ever a
//     discarded PICK candidate. NOTE the super-argument range is the FLAT literal
//     (54, 60) at every ascension -- only the setHp under it is branched -- so
//     the roll row has a `base` column and nothing else.
//
// (2) THE A18 SELF-STRENGTH IS AN ASCENSION *PRESENCE* BRANCH, not an amount.
//     `if (ascensionLevel < 18) break;` (:73) guards an
//     `ApplyPowerAction(this, this, new StrengthPower(this, 1), 1)`. A registry
//     effect list expresses per-tier AMOUNTS and not per-tier PRESENCE (the
//     Snecko TAIL / GremlinFat Frail limitation), so the step is queued natively
//     after the row's program, with both sides of the branch spelled out. At the
//     engine's fixed A20 it is always present.
//
// (3) damage.get(0) IS DEAD AND die() NEEDS NO SEAM. `damage.add(new
//     DamageInfo(this, 4))` (:58) is the WHIP, and nothing ever queues it:
//     getMove (:81-84) sets move 2 unconditionally and takeTurn has no other
//     case. So no move row carries a 4, deliberately. die() (:104-108) is
//     `super.die()` then playDeathSfx, whose `MathUtils.random(1)` (:96) is
//     UNSEEDED libGDX -- the Looter's answer, not the Mugger's -- so the class
//     gets an explicit nullptr in monster_die_fn rather than an entry.
//
// getMove ignores its `num`, but the draw that produced it is real: rollMove
// still calls `aiRng.random(99)` at init AND on every turn (the trailing
// RollMoveAction at :78), and both move the shared stream.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

void taskmaster_init(CombatState& state, uint8_t monster_index) noexcept;
void taskmaster_take_turn(CombatState& state, uint8_t monster_index) noexcept;

// The trailing RollMoveAction (:78) sits outside the switch. Queued rather than
// inline for the Fat gremlin's reason: the Taskmaster's own attack can queue
// player Thorns/Flame Barrier damage ahead of it, and RollMoveAction has no
// liveness check (RollMoveAction.java:17-21), so a dead Taskmaster still rolls.
void taskmaster_roll_move(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
