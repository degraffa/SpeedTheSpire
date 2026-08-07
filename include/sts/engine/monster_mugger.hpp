#pragma once

// The Mugger (the Act-2 group "2 Thieves", MonsterHelper.java:462-464, where it
// spawns SECOND, behind a Looter). HP range and move effects are generated
// registry data (monsters.yaml id 32); move SELECTION is native, and -- exactly
// like the Looter's -- it is not a roll-driven tree at all: getMove
// (Mugger.java:167-170) IGNORES its rolled num and every later transition is
// decided inside takeTurn by slashCount plus aiRng draws.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), all read in full:
//   Mugger.java:32-184, PLUS the recovered anonymous inner classes Mugger$1 and
//   Mugger$2 (the stolen-gold accrual actions; see RECOVERED-INNER-CLASSES.md at
//   the decompile root for the recovery provenance and the byte-identity proof);
//   ThieveryPower.java:11-31; DamageAction.java:38-42 (the 3-arg stealGold ctor)
//   and :98-114 (stealGold itself); EscapeAction.java:13-29;
//   AbstractMonster.java:705-715,765-775 (init/rollMove/setHp),
//   894-906 (updateEscapeAnimation), 915-919 (escape()), 925-951 (die()).
//
// THE STATE MACHINE (takeTurn, Mugger.java:86-136) is the Looter's with three
// differences, each of which is a real behavioural fork rather than a renaming:
// Mug -> Mug; on the SECOND Mug a 50/50 picks Smoke Bomb or Big Swipe; Big Swipe
// telegraphs Smoke Bomb unconditionally; Smoke Bomb gains block and telegraphs
// Escape; Escape marks the room mugged and leaves.
//
// FIVE THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) AI-RNG ACCOUNTING -- AND IT IS NOT THE LOOTER'S. Both thieves make one
//     discarded ai_rng.random(99) at init() and neither ever queues a
//     RollMoveAction. From there they diverge:
//
//       * playSfx (Mugger.java:138-145) draws a SEEDED aiRng.random(2), once per
//         MUG and once per BIG SWIPE. The Looter's identically-shaped playSfx
//         rolls libGDX MathUtils -- an UNSEEDED global generator -- and costs
//         nothing (Looter.java:137-143). So a Mugger's attacks move the shared
//         stream and a Looter's do not.
//       * playDeathSfx (:147-154) draws ANOTHER seeded aiRng.random(2), from
//         die(). The Looter's death draws nothing seeded at all. This is why the
//         batch needed a MonsterDieFn slot (monster_dispatch.hpp).
//       * The TALK GATE FIRES ON THE SECOND MUG, NOT THE FIRST:
//         `slashCount == 1 && aiRng.randomBoolean(0.6f)` (:91), against the
//         Looter's `slashCount == 0 && ...` (:92 there). Java's && short-circuits
//         both ways, so the 0.6 coin is DRAWN only on the Mug where the count
//         matches -- the second here, the first there.
//       * The 0.5 Smoke-Bomb-or-Big-Swipe coin at slashCount == 2 (:99) is the
//         one draw the two thieves share exactly.
//
// (2) pad0 IS slashCount IS THE STEAL COUNT -- the Looter's shape, reused
//     deliberately. slashCount increments on exactly the two moves that steal
//     (Mug :97, Big Swipe :111), so one counter serves both meanings; it
//     saturates at 3, the machine's maximum (Mug, Mug, Big Swipe). NO new
//     MonsterState.flags bit and no new field: a record is never both a Looter
//     and a Mugger, and pad0 is explicitly type-scoped scratch
//     (combat_state.hpp).
//
//     The Java's separate `stolenGold` field (:55) accrues
//     min(goldAmt, player.gold) per attack through a QUEUED anonymous action
//     (Mugger$1 at :95 / Mugger$2 at :113), one slot ahead of the DamageAction
//     whose stealGold (DamageAction.java:98-114) clamps to the same purse and
//     deducts it. Both therefore read the SAME purse at the same instant, which
//     is what makes the per-steal clamp well-defined -- see monster_looter.hpp
//     for the settlement that reproduces it across BOTH thieves.
//
// (3) THE ESCAPE IS TWO SYNCHRONOUS HALVES AND ONE QUEUED ONE, identically to
//     the Looter: room.mugged is set synchronously inside takeTurn (:129 ->
//     kCombatFlagMugged), the EscapeAction is queued (:131 -> the ESCAPE opcode,
//     which sets kMonsterFlagEscaped at resolve time), and the re-telegraphing
//     SetMoveAction follows (:132). Note the Mugger's SMOKE_BOMB case has NO
//     TalkAction where the Looter's does (:120-125 there) -- presentation only,
//     but it is the reason the two bodies are not one shared function.
//
// (4) THE BLOCK NUMBERS ARE NOT SHARED. escapeDef is a FIELD INITIALIZER of 11
//     (:47) and the A17 arm adds a literal 6 (:120), so Smoke Bomb gains 11 or 17
//     -- against the Looter's flat 6 (Looter.java:46). Registry columns.
//
// (5) NO damage() OVERRIDE, NO ROLL-MOVE FN, NO SPAWN PATH. Mugger.java declares
//     usePreBattleAction, takeTurn, playSfx, playDeathSfx, die and getMove --
//     nothing else -- so there is no on_monster_damaged case; no takeTurn body
//     queues ROLL_MOVE, so no monster_roll_move_fn registration; nothing splits
//     or summons a Mugger. It DOES register a MonsterDieFn, for note (1).

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Mugger.goldAmt (Mugger.java:61): 20 from A17, 15 below. The engine's fixed
// difficulty is A20, so the A17 branch is the live one. Also the applied
// ThieveryPower's amount (:83). Numerically equal to the Looter's, which is a
// coincidence of the two rows and NOT a shared constant -- they are separate
// fields in separate classes, and thief_gold_amount (monster_looter.hpp) keeps
// them separate on purpose.
inline constexpr int32_t kMuggerGoldAmt = 20;

// MonsterState.pad0 for the Mugger: slashCount (Mugger.java:54), the WHOLE byte
// (monster-type-scoped scratch, no global layout). Doubles as the steal count --
// see note (2) above.
[[nodiscard]] inline uint8_t mugger_steal_count(const MonsterState& m) noexcept {
    return m.pad0;
}

void mugger_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (Mugger.java:81-84): queue ApplyPowerAction(this, this,
// ThieveryPower(this, goldAmt)) -- a pure marker power (powers.yaml id 75), the
// same one the Looter applies. No RNG draw.
void mugger_use_pre_battle_action(CombatState& state,
                                  uint8_t monster_index) noexcept;

void mugger_take_turn(CombatState& state, uint8_t monster_index) noexcept;

// die() (Mugger.java:156-165): playDeathSfx' ONE SEEDED aiRng.random(2), before
// `super.die()`. The stolen-gold half of that method
// (addStolenGoldToRewards, :161-163) is NOT here -- it is a READ of the surviving
// record by the reward layer (settle_stolen_gold, run_advance.cpp), exactly as
// the Looter's is.
void mugger_die(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
