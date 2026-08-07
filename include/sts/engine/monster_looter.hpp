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
//     the player's gold and deducts it. Both read the SAME purse at the same
//     instant, which is what makes the per-steal clamp well-defined. The engine
//     stores the COUNT and settles at combat end -- see thief_stolen_gold and
//     settle_stolen_gold below, which reconstruct the per-steal clamping rather
//     than approximating it.
//
//     HISTORY: this used to be a sum-then-clamp, min(count * goldAmt, gold),
//     and that was a recorded deviation naming an Act-2-adjacent task as its
//     owner. S2.22 is that task and the deviation is GONE. The two forms agree
//     on the TOTAL taken (min over a shrinking purse telescopes to
//     min(total_requested, initial_purse)) and disagree on ATTRIBUTION as soon
//     as a group fields TWO thieves whose fates differ -- which "2 Thieves"
//     (Looter + Mugger, MonsterHelper.java:462-464) does. See settle_stolen_gold
//     for how the order is reconstructed.
//
// (3) THE ESCAPE IS TWO SYNCHRONOUS HALVES AND ONE QUEUED ONE. room.mugged is
//     set synchronously inside takeTurn (:128 -> kCombatFlagMugged), the
//     EscapeAction is queued (:130 -> the ESCAPE opcode, which sets
//     kMonsterFlagEscaped at resolve time), and the pump's liveness predicate
//     (monster_basically_dead) then ends the battle if nobody is left in the
//     fight. The stolen gold's two fates stay distinct in the terminal state:
//     KILLED -- hp <= 0, die() returns stolenGold via the reward screen
//     (addStolenGoldToRewards, :170-172, BEFORE super.die()'s power/relic
//     walks); ESCAPED -- hp > 0 + kMonsterFlagEscaped + kCombatFlagMugged, the
//     gold is gone. Both are reads on the surviving record (thief_stolen_gold
//     below), and the consumer EXISTS (this line used to say it did not):
//     run_advance.cpp settle_stolen_gold computes the dead thieves' clamped
//     return and seed_combat_rewards presents it as the STOLEN_GOLD reward
//     item (combat_rewards.cpp), with no Golden Idol bonus on the theft
//     branch.
//
// (4) NO damage() OVERRIDE, NO ROLL-MOVE FN, NO SPAWN PATH. Looter.java
//     declares usePreBattleAction, takeTurn, playSfx, playDeathSfx, die and
//     getMove -- no damage(), so no on_monster_damaged case; no takeTurn body
//     queues ROLL_MOVE, so no monster_roll_move_fn registration; nothing
//     splits or summons a Looter, so no spawn-at-hp fn.

#include <cstdint>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/monster_mugger.hpp"  // kMuggerGoldAmt (the second thief)

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

// --- The thief interface -----------------------------------------------------
//
// TWO monster types steal gold: the Looter (id 26) and the Mugger (id 32). They
// are separate classes with separate fields and separate sound behaviour, but
// their ACCOUNTING is identical -- a per-attack steal of `goldAmt`, clamped at
// steal time to the player's remaining purse -- so the settlement layer treats
// them through the three helpers below rather than by naming ids twice.
//
// A NEW THIEF IS A ONE-LINE CHANGE HERE plus its id in the settlement walk; the
// helpers are written so that forgetting the second half is a zero return rather
// than a wrong number.

// Is this record a gold-stealing monster at all?
[[nodiscard]] inline bool is_thief(const MonsterState& m) noexcept {
    const MonsterId id = static_cast<MonsterId>(m.monster_id);
    return id == MonsterId::LOOTER || id == MonsterId::MUGGER;
}

// This thief's per-steal `goldAmt`. NOT a shared constant: Looter.goldAmt
// (Looter.java:63) and Mugger.goldAmt (Mugger.java:61) are separate fields in
// separate classes that happen to carry the same 20 at A17+, and a future thief
// (or a future ascension column) need not agree with either.
[[nodiscard]] inline int32_t thief_gold_amount(MonsterId id) noexcept {
    switch (id) {
        case MonsterId::LOOTER:
            return kLooterGoldAmt;
        case MonsterId::MUGGER:
            return kMuggerGoldAmt;
        default:
            return 0;
    }
}

// How many steals this thief has made -- pad0 for both types (see note (2)).
[[nodiscard]] inline uint8_t thief_steal_count(const MonsterState& m) noexcept {
    return is_thief(m) ? m.pad0 : static_cast<uint8_t>(0);
}

// The gold this thief has REQUESTED so far: steals x goldAmt, UNCLAMPED. The
// clamp is not a property of the thief -- it is a property of the purse at each
// steal -- so it is applied by settle_stolen_gold (run_advance.cpp), which knows
// both the purse and the interleaving of every thief's steals.
[[nodiscard]] inline int32_t thief_stolen_gold(const MonsterState& m) noexcept {
    return static_cast<int32_t>(thief_steal_count(m)) *
           thief_gold_amount(static_cast<MonsterId>(m.monster_id));
}

// The Looter-specific spelling, kept because it is what the Looter's own tests
// and the replay tool's readout name. Identical to thief_stolen_gold on a Looter
// record; prefer thief_stolen_gold in anything that walks a whole group.
[[nodiscard]] inline int32_t looter_stolen_gold(const MonsterState& m) noexcept {
    return thief_stolen_gold(m);
}

void looter_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (Looter.java:84-86): queue ApplyPowerAction(this, this,
// ThieveryPower(this, goldAmt)) -- a pure marker power (powers.yaml id 75).
// No RNG draw.
void looter_use_pre_battle_action(CombatState& state,
                                  uint8_t monster_index) noexcept;

void looter_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
