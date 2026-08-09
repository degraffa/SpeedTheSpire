#pragma once

// The Transient (the solo Act-3 STRONG group "Transient", encounters.yaml id
// 48). Stats and move effects are generated registry data (monsters.yaml id 55);
// move SELECTION is native -- and, like the Spheric Guardian's, it is native
// despite reading no randomness at all.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   Transient.java:30-116; FadingPower.java:16-47; ShiftingPower.java:15-47;
//   GainStrengthPower.java (the Shackled half of the swap);
//   SuicideAction.java:12-38; AbstractMonster.java:135-155 (the zero-RNG ctor),
//   705-715 (init/rollMove), 431-445 (setMove pushes moveHistory);
//   GameActionManager.java:300-360 (the `takeTurn(); applyTurnPowers();` pair);
//   AbstractCreature.java:69 (the `gold` field), 529-547.
//
// A 999-HP MONSTER THAT KILLS ITSELF ON A TIMER. Nothing about the fight is
// about its health bar: FadingPower counts down 6 turns at A17+ (5 below) and
// then SuicideActions it, so the whole encounter is a race between the player's
// clock and a damage ramp that adds 10 every turn.
//
// SIX THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) ZERO monster_hp_rng DRAWS. setHp is NEVER called: the ctor hands maxHealth
//     999 straight to super (:46) and AbstractMonster's ctor
//     (AbstractMonster.java:135-155) assigns `currentHealth = maxHealth` with no
//     RNG anywhere in it. The Spheric Guardian shape, and the init below SKIPS
//     the draw rather than calling random() over a degenerate 999..999 range,
//     which would still consume a value. Contrast the Spire Growth and the
//     Writhing Mass in this SAME BATCH, which call the one-arg setHp and do draw.
//
// (2) EXACTLY ONE ai_rng DRAW IN THE WHOLE COMBAT, AND IT IS DISCARDED. getMove
//     (:112-115) ignores `num` entirely and unconditionally re-arms move 1; and
//     takeTurn has NO trailing RollMoveAction -- it calls setMove on itself
//     instead (:81). So after init's rollMove the Transient never touches the
//     stream again. Native rather than an empty table precisely because that one
//     discarded draw still moves the shared stream. (The Looter / Guardian /
//     Red Slaver precedent.)
//
// (3) takeTurn's OWN setMove STILL PUSHES MOVE HISTORY (AbstractMonster.java:
//     431-437), so the ring fills with 1s turn after turn even though nothing
//     ever reads it. Reproduced, because move_history is oracle-compared.
//
// (4) THE DAMAGE RAMP IS PER-INSTANCE, NOT A TIER COLUMN. takeTurn reads
//     `damage.get(this.count)` (:79) out of seven DamageInfos built at
//     startingDeathDmg + 10*i (:54-60), then increments the count and
//     re-telegraphs `startingDeathDmg + count * 10`. `count` lives in
//     MonsterState.pad0 -- the monster-type-scoped scratch byte -- and the
//     registry row carries only rung zero. At A20 the live ladder is
//     40/50/60/70/80/90: SIX rungs, because Fading 6 kills it at the end of its
//     sixth turn. The seventh DamageInfo (100) is unreachable at every
//     ascension.
//
// (5) FADING FIRES **AFTER** takeTurn. GameActionManager.java:322-323 is
//     `m.takeTurn(); m.applyTurnPowers();`, so the Transient attacks on the turn
//     it dies. The suicide is the ONE-ARG SuicideAction, i.e. triggerRelics
//     TRUE, so the full death edge runs. See power_fading.hpp.
//
// (6) NO MonsterDieFn. die() (:106-110) is `super.die()` plus
//     UnlockTracker.unlockAchievement("TRANSIENT") -- meta-progression, no
//     combat state, no seeded draw. And the `gold = 1` the ctor sets (:50) is
//     DEAD: nothing in rooms/ or dungeons/ reads a monster's gold field, so
//     SuicideAction's `m.gold = 0` (SuicideAction.java:31) changes no reward.
//     A faded-out Transient and a killed one pay the same, which is a named
//     test rather than a silent assumption.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Transient's flat HP (Transient.java:37,46) -- the ctor argument, at every
// ascension, with no setHp call and so no monster_hp_rng draw.
inline constexpr int16_t kTransientHp = 999;

// FadingPower's turn count (Transient.java:65-69): 5, or 6 at A17+. A monster
// constant rather than a registry column because it is an argument the MONSTER
// passes to a power, not one of the monster's own stats.
inline constexpr int32_t kTransientFadingTurns = 5;
inline constexpr int32_t kTransientFadingTurnsA17 = 6;

// ShiftingPower's ctor assigns NO amount (ShiftingPower.java:22-29), so the
// applied instance carries AbstractPower's field-initializer -1
// (AbstractPower.java:65) -- exactly as Confusion's does, and exactly what the
// 3-arg ApplyPowerAction then hands to addPower. Not 0, and not 1.
inline constexpr int32_t kShiftingNoAmount = -1;

// The ramp step: `startingDeathDmg + this.count * 10` (Transient.java:54-60,81).
inline constexpr int32_t kTransientDamageStep = 10;

// The number of pre-built DamageInfos (Transient.java:54-60). The count index
// saturates here rather than running off the end; see the body for why that is
// unreachable at every ascension.
inline constexpr int32_t kTransientDamageRungs = 7;

void transient_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (Transient.java:63-71), TWO powers in this order:
// FadingPower(this, A17 ? 6 : 5) then ShiftingPower(this). No RNG draw.
void transient_use_pre_battle_action(CombatState& state,
                                     uint8_t monster_index) noexcept;

void transient_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
