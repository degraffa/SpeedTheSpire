#pragma once

// The Snecko (the solo Act-2 STRONG group "Snecko", MonsterHelper.java:495-497,
// built through the NO-ARG ctor). Stats and move effects are generated registry
// data (monsters.yaml id 34); move SELECTION is native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   Snecko.java:36-165; ConfusionPower.java:16-58;
//   ApplyPowerAction.java:80-82 (the 3-arg ctor) and :96-180 (update);
//   AbstractPower.java:65 (the amount field initializer), :152-159 (stackPower);
//   AbstractCreature.java:506-527 (addPower);
//   AbstractMonster.java:705-715 (init/rollMove), 765-775 (setHp), 431-491.
//
// THE MOVE TREE (getMove, Snecko.java:141-158) HAS NO ASCENSION BRANCH AT ALL --
// the only monster in this batch of which that is true, and worth saying because
// three of its four siblings are two-armed:
//
//   1. firstTurn  -> GLARE (STRONG_DEBUFF), and return WITHOUT reading num.
//   2. num < 40   -> TAIL  (ATTACK_DEBUFF)
//   3. lastTwoMoves(BITE) ? TAIL : BITE (ATTACK)
//
// FIVE THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) `firstTurn` NEEDS NO STATE, and the batch's granted pad0 slot for it is
//     RELEASED. Step 1 is true on exactly one call -- init()'s rollMove, which
//     is the only getMove entry point that precedes the first queued
//     RollMoveAction -- and false on every later one, so it IS "is this the init
//     call", which this module's init/roll split already answers structurally.
//     Identical to the Chosen's `usedHex` and the Red Slaver's `firstTurn`
//     (monster_chosen.hpp / monster_slaver.hpp). snecko_init therefore
//     telegraphs GLARE directly and snecko_roll_move implements only steps 2
//     and 3. It would become real storage only if some entry point could reach
//     getMove twice before the first roll, which none does.
//
// (2) AI-RNG ACCOUNTING. One ai_rng.random(99) at init(), DISCARDED -- step 1
//     returns before num is read (the Chosen / Looter / Guardian precedent) --
//     and then exactly one ai_rng.random(99) per turn through the trailing
//     RollMoveAction (:120), which sits OUTSIDE takeTurn's switch so all three
//     move bodies reach it. No branch spends a second draw. One monster_hp_rng
//     draw in the ctor, over the A7 column (120, 125). The BiteEffect jitter in
//     BITE (:103) is unseeded MathUtils.
//
// (3) THE OPENING GLARE IS UNCONDITIONAL AT EVERY SEED. Because step 1 does not
//     read num, a Snecko's first move is GLARE no matter what the init draw
//     produced -- and the draw still advances the shared stream, so the
//     discarding is observable in what the NEXT monster rolls.
//
// (4) CONFUSION'S APPLIED AMOUNT IS -1, NOT 1, AND THAT IS EVIDENCE-BACKED.
//     GLARE applies `new ConfusionPower(player)` through the 3-ARG
//     ApplyPowerAction (:97), whose stack amount is `powerToApply.amount`
//     (ApplyPowerAction.java:80-82). ConfusionPower's ctor takes no amount and
//     assigns none (ConfusionPower.java:23-31), so that is AbstractPower's field
//     initializer -1 (AbstractPower.java:65) -- and on a NEW slot the game adds
//     the power OBJECT itself (AbstractCreature.addPower :506-513 /
//     ApplyPowerAction.update :164-166), so -1 is what the slot holds and what
//     the oracle reports. Confirmed against a live capture rather than reasoned
//     alone: tests/golden/oracle_corpus/act1_a20_50 carries
//     `{"amount": -1, "name": "Confusion", "id": "Confusion"}` on the player's
//     power list. The engine used to write 1 (from Snecko Eye, the S1 producer);
//     that is corrected in the same commit as this monster, in relics_boss.cpp
//     and here, because GLARE makes Confusion MONSTER-applied in combat for the
//     first time and the two producers must not disagree. The amount is
//     behaviourally inert -- nothing reads it, the native onCardDraw body
//     ignores it -- but it is oracle-visible, which is the whole point.
//
//     A SECOND APPLICATION IS A NO-OP, for the same reason: AbstractPower.
//     stackPower returns immediately when `amount == -1` (:153-156), so a Snecko
//     GLARE landing on a player who already carries Snecko Eye's Confusion
//     leaves the slot at -1 rather than moving it to -2. op_apply_power carries
//     that guard (interp/interp_powers.cpp).
//
// (5) TAIL'S WEAK STEP IS ASCENSION-GATED BY PRESENCE. `if (ascensionLevel >= 17)
//     ApplyPowerAction(Weak 2)` (:112-114) sits BETWEEN the damage and the
//     always-applied Vulnerable, so the order is Damage -> Weak -> Vulnerable and
//     the middle step simply does not exist below A17. An effect list can express
//     per-tier AMOUNTS but not per-tier PRESENCE (the Byrd peckCount limitation),
//     so this ONE move is queued step-by-step by snecko_take_turn, which skips
//     the Weak step below A17 -- the registry row still carries all three steps
//     and remains the pinned amount source. Every other move goes through
//     queue_monster_move_effects unchanged.
//
// NO damage() CONSEQUENCE, NO SPAWN PATH. Snecko.java declares takeTurn,
// changeState, damage, getMove and die. changeState's two keys ("ATTACK",
// "ATTACK_2", :124-135) are spine calls; damage() (:138-144) is super.damage plus
// the standard hit animation, so an empty on_monster_damaged hook is complete;
// die() (:161-164) is `super.die()` then ONE UNSEEDED sound, so it needs no
// MonsterDieFn (contrast the Mugger's, which is seeded). Nothing splits or
// summons a Snecko.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// ConfusionPower's applied amount: AbstractPower's field initializer -1
// (AbstractPower.java:65), reached through the 3-arg ApplyPowerAction. See note
// (4) above for the oracle evidence. Shared with the Snecko Eye relic
// (relics/relics_boss.cpp) so the two producers cannot drift apart.
inline constexpr int32_t kConfusionAppliedAmount = -1;

// TAIL's Weak step (Snecko.java:112-114) is inside `if (ascensionLevel >= 17)`,
// so its PRESENCE -- not its amount -- is what the ascension decides. Exposed as
// a predicate rather than written inline so the tier-2 tests can pin BOTH sides
// of a boundary the fixed-A20 engine can otherwise only ever exercise one way.
[[nodiscard]] inline constexpr bool snecko_tail_applies_weak(
    int32_t ascension) noexcept {
    return ascension >= 17;
}

void snecko_init(CombatState& state, uint8_t monster_index) noexcept;

// The trailing RollMoveAction (Snecko.java:120), reached by all three takeTurn
// cases.
void snecko_roll_move(CombatState& state, uint8_t monster_index) noexcept;

void snecko_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
