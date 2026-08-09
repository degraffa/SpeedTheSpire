#pragma once

// The Nemesis (registry/monsters.yaml id 59, MonsterId::NEMESIS) -- the Act-3
// solo elite that is Intangible every other round (Nemesis.java, 232 lines,
// read in full).
//
// FIVE READINGS THIS MODULE LEANS ON:
//
// (1) THE INTANGIBLE IS THE MONSTER-SIDE CLASS, AND IT IS APPLIED OUTSIDE THE
//     SWITCH. takeTurn ends with
//         if (!this.hasPower("Intangible"))
//             addToBottom(new ApplyPowerAction(this, this, new IntangiblePower(this, 1)));
//         addToBottom(new RollMoveAction(this));
//     (:114-117), so EVERY move -- Scythe, Tri-Attack, Tri-Burn -- re-arms it
//     when it is absent, and the guard is a live read of the power list at
//     queue time. IntangiblePower ("Intangible", powers.yaml 107) is NOT
//     IntangiblePlayerPower ("IntangiblePlayer", id 29): different class,
//     different POWER_ID literal, different decay hook (atEndOfTURN, not
//     atEndOfROUND). See power_intangible_monster.hpp.
//
//     The resulting rhythm, at amount 1: applied on the monster's turn N; the
//     end-of-round tick for round N is spent clearing `justApplied`; the tick
//     for round N+1 reduces it to zero and removes it, at the START of the
//     player's turn N+2. So the player gets exactly one turn in three where the
//     Nemesis is hittable for real damage, and the re-application on turn N+2
//     closes it again.
//
// (2) THE CAP LIVES AT TWO SITES, NOT ONE. IntangiblePower.atDamageFinalReceive
//     (:42-47) caps NORMAL damage at 1 inside DamageInfo.applyPowers. Nemesis
//     ALSO overrides damage() (:120-131) with
//         if (info.output > 0 && this.hasPower("Intangible")) info.output = 1;
//     BEFORE super.damage(info) -- i.e. before decrementBlock, and with NO
//     DamageType test, so it also catches THORNS and HP_LOSS, which skip
//     applyPowers entirely. That is the same two-site shape AbstractPlayer.damage
//     (:1397-1399) gives the player-side power, and interp_damage.cpp's
//     intangible_cap now spells both. Note the guard is `> 0`, not `> 1`; the
//     two are indistinguishable in effect (capping a 1 at 1 changes nothing) and
//     the Java's form is what is written.
//
//     IT IS KEYED ON THE MONSTER TYPE, not on "any monster with Intangible",
//     because the second site is a Nemesis METHOD OVERRIDE. Nothing else in the
//     game gives a monster Intangible, so today the distinction is invisible --
//     but the faithful statement is the one that survives Act 4.
//
// (3) getMove SPENDS ONE OR TWO ai_rng DRAWS PER TURN, and which is which is not
//     obvious from the shape. On top of the rollMove draw that produced `num`,
//     three arms consult `AbstractDungeon.aiRng.randomBoolean()` (:161,175,187).
//     The last of them is `if (aiRng.randomBoolean() && this.scytheCooldown <= 0)`
//     (:187) -- Java evaluates the left operand FIRST, so THE DRAW IS TAKEN EVEN
//     WHEN THE COOLDOWN WOULD HAVE REFUSED THE SCYTHE. Reordering that
//     conjunction to test the cheap integer first is the natural "optimisation"
//     and it silently desynchronises the shared ai stream.
//
// (4) `scytheCooldown` IS DECREMENTED AT THE TOP OF EVERY getMove (:147), before
//     any branch and including the firstMove arm -- so a rollMove that decides
//     nothing interesting still ages the cooldown. It is set to 2 at each of the
//     three Scythe sites. The Java lets it run arbitrarily negative; this module
//     FLOORS IT AT ZERO in MonsterState::pad0, which is exact because every
//     reader is `<= 0` or `> 0`.
//
// (5) THE BURNS GO TO THE DISCARD PILE. MakeTempCardInDiscardAction on both
//     ascension arms (:108,111), 3 Burns or 5 from A18. There is no draw-pile
//     variant anywhere in the class -- worth stating because the sibling status
//     generators in this act do split by pile.
//
// die() (:213-217) is playDeathSfx then super.die(), and playDeathSfx rolls
// UNSEEDED MathUtils (:204-211) -- so the Nemesis registers an explicit nullptr
// MonsterDieFn (the Taskmaster/Book of Stabbing reading).

#include <cstdint>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// SCYTHE_COOLDOWN_TURNS (Nemesis.java:47): the value the cooldown is reset to at
// each of the three Scythe sites.
inline constexpr int32_t kNemesisScytheCooldown = 2;

[[nodiscard]] inline int32_t nemesis_scythe_cooldown(
    const MonsterState& m) noexcept {
    return static_cast<int32_t>(m.pad0);
}
// Floored at zero -- see header note (4); every reader is `<= 0` or `> 0`, so a
// floor is indistinguishable from the Java's unbounded negative.
inline void nemesis_set_scythe_cooldown(MonsterState& m, int32_t v) noexcept {
    m.pad0 = static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

void nemesis_init(CombatState& state, uint8_t monster_index) noexcept;
void nemesis_take_turn(CombatState& state, uint8_t monster_index) noexcept;
void nemesis_roll_move(CombatState& state, uint8_t monster_index) noexcept;

// getMove (:145-193), exposed for the directed tier-2 tests. `num` is the rolled
// 0..99; the extra randomBoolean draws come off `state.ai_rng` inside.
void nemesis_decide_move(CombatState& state, uint8_t monster_index,
                         int32_t num) noexcept;

}  // namespace sts::engine
