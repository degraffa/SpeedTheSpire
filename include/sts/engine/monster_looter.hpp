#pragma once

// The Looter (MonsterHelper.getEncounter "Looter", MonsterHelper.java:400-402;
// also reachable through the Exordium Thugs group's bottomGetStrongHumanoid,
// :816-829). HP range and move effects are generated registry data; move
// SELECTION is native -- and unlike the slavers it is not a roll-driven tree at
// all: getMove (Looter.java:176-179) IGNORES its rolled num, and every later
// transition is decided inside takeTurn by slashCount plus two aiRng
// randomBoolean draws. The fixed S1 difficulty is A20, so the A17 gold and A7
// HP branches are the live ones.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), all read in full:
//   Looter.java:33-193; ThieveryPower.java:11-31;
//   DamageAction.java:22-115 (the 3-arg stealGold ctor, :39-42, and
//   stealGold(), :98-114); EscapeAction.java:13-29;
//   AbstractMonster.java:705-715,765-775 (init/rollMove/setHp),
//   894-906 (updateEscapeAnimation), 915-919 (escape());
//   MonsterGroup.java:90-95,117-122 (the liveness predicate this monster
//   landed together with).
//
// THE STATE MACHINE (takeTurn, Looter.java:88-135). The opener is always Mug
// (getMove discards its num). Mug -> Mug; the second attack flips a 50/50
// (aiRng.randomBoolean(0.5f), :101): true telegraphs Smoke Bomb, false
// telegraphs Lunge -- and Lunge then telegraphs Smoke Bomb unconditionally
// (:117). Smoke Bomb gains 6 block and telegraphs Escape (:120-124); Escape
// marks the room mugged, queues the EscapeAction and re-telegraphs Escape
// (:126-132). So the whole combat is Mug, Mug, [Smoke Bomb | Lunge, Smoke
// Bomb], Escape -- escape on turn 4 or 5, REACHABLE in Act 1, unlike the
// gremlins' caller-less move 99.
//
// FOUR THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) AI-RNG ACCOUNTING. One ai_rng.random(99) at init() (discarded by
//     getMove); NO queued RollMoveAction ever (the Guardian precedent); then
//     exactly two randomBoolean draws across the whole combat -- the 0.6
//     first-Mug talk gate (:92, drawn ONLY while slashCount == 0 because
//     Java's && short-circuits) and the 0.5 coin when slashCount reaches 2
//     (:101). The Lunge turn rolls nothing. playSfx/playDeathSfx roll
//     MathUtils.random -- libgdx's global generator, not a seeded stream
//     (contrast the Act-2 Mugger, which rolls aiRng for the same sounds,
//     Mugger.java:139,149).
//
// (2) pad0 IS slashCount IS THE STEAL COUNT. slashCount (:56) increments on
//     exactly the two moves that steal (Mug :99, Lunge :113), so one counter
//     serves both meanings; it saturates at 3, the machine's maximum. The
//     Java's separate stolenGold field (:57) accrues min(goldAmt, player.gold)
//     per attack via an anonymous queued action (:97,115 -- the synthetic
//     accessors :181-192 are its decompiled residue), one queue slot ahead of
//     the DamageAction whose stealGold (DamageAction.java:98-114) clamps to
//     the player's gold and deducts it. CombatState carries NO gold field, so
//     the engine stores the count and the settlement layer computes
//     min(count * goldAmt, gold) at combat end. That sum-then-clamp equals the
//     game's per-steal clamping whenever the thief's steals are the only gold
//     movement in the combat -- true for every Act-1 group, since none fields
//     two thieves ("Looter" is solo; "Exordium Thugs" emits at most one). An
//     Act-2 owner adding the Mugger to a shared group must revisit this.
//
// (3) THE ESCAPE IS TWO SYNCHRONOUS HALVES AND ONE QUEUED ONE. room.mugged is
//     set synchronously inside takeTurn (:128 -> kCombatFlagMugged), the
//     EscapeAction is queued (:130 -> the ESCAPE opcode, which sets
//     kMonsterFlagEscaped at resolve time), and the pump's liveness predicate
//     (monster_dead_or_escaped) then ends the battle if nobody is left in the
//     fight. The stolen gold's two fates stay distinct in the terminal state:
//     KILLED -- hp <= 0, die() returns stolenGold via the reward screen
//     (addStolenGoldToRewards, :170-172, BEFORE super.die()'s power/relic
//     walks); ESCAPED -- hp > 0 + kMonsterFlagEscaped + kCombatFlagMugged, the
//     gold is gone. Both are reads on the surviving record (looter_stolen_gold
//     below); the reward screen that consumes them does not exist yet and
//     belongs to the combat-rewards layer.
//
// (4) NO damage() OVERRIDE, NO ROLL-MOVE FN, NO SPAWN PATH. Looter.java
//     declares usePreBattleAction, takeTurn, playSfx, playDeathSfx, die and
//     getMove -- no damage(), so no on_monster_damaged case; no takeTurn body
//     queues ROLL_MOVE, so no monster_roll_move_fn registration; nothing
//     splits or summons a Looter, so no spawn-at-hp fn.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Looter.goldAmt (Looter.java:63): 20 from A17, 15 below. The engine's fixed
// S1 difficulty is A20, so the A17 branch is the live one. Also the applied
// ThieveryPower's amount (:85).
inline constexpr int32_t kLooterGoldAmt = 20;

// MonsterState.pad0 for the Looter: slashCount (Looter.java:56), the WHOLE
// byte (monster-type-scoped scratch, no global layout). Doubles as the steal
// count -- see note (2) above.
[[nodiscard]] inline uint8_t looter_steal_count(const MonsterState& m) noexcept {
    return m.pad0;
}

// The gold this Looter has taken so far, UNCLAMPED (the game clamps each steal
// to the player's remaining gold at steal time; the engine's settlement layer
// applies min(total, gold) at combat end instead -- exact per note (2)).
// On a KILLED record this is what die() returns to the rewards; on an ESCAPED
// record it is what the player lost.
[[nodiscard]] inline int32_t looter_stolen_gold(const MonsterState& m) noexcept {
    return static_cast<int32_t>(m.pad0) * kLooterGoldAmt;
}

void looter_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (Looter.java:84-86): queue ApplyPowerAction(this, this,
// ThieveryPower(this, goldAmt)) -- a pure marker power (powers.yaml id 75).
// No RNG draw.
void looter_use_pre_battle_action(CombatState& state,
                                  uint8_t monster_index) noexcept;

void looter_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
