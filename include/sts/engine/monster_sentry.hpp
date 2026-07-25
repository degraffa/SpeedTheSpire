#pragma once

// Sentry monster module. Stats/move-effects are DATA from registry/monsters.yaml
// (generated kSentry); move *selection* and the pre-battle Artifact are the
// native code below (design §4.2), matching the cultist_*/louse_* split.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled, read in full):
//   * Sentry ctor (Sentry.java:59-77): ELITE (:61); setHp is (39,45) from A8 else
//     (38,42) (:62-66) -- one monster_hp_rng draw, currentHealth == maxHealth;
//     beamDmg 10 from A3 else 9 (:67); dazedAmt 3 from A18 else 2 (:68).
//   * usePreBattleAction (Sentry.java:79-82): addToBottom
//     ApplyPowerAction(this, this, new ArtifactPower(this, 1)) -- ONE Artifact
//     stack on each Sentry, which nullifies (and is consumed by) the first debuff
//     aimed at it. The nullify itself is the shared APPLY_POWER interception
//     (ApplyPowerAction.java:131-138), already live in interp/interp_powers.cpp;
//     this module only grants the stack. No RNG.
//   * takeTurn (Sentry.java:84-113): BOLT queues
//     MakeTempCardInDiscardAction(new Dazed(), dazedAmt) -- fresh Dazed copies
//     into the DISCARD pile, not the hand (:96); BEAM deals damage[0] (:108).
//     Both cases fall through to an unconditional RollMoveAction (:112), so
//     exactly one ai_rng.random(99) draw per turn.
//   * getMove (Sentry.java:134-150): the FIRST decision is keyed on POSITION --
//     `AbstractDungeon.getMonsters().monsters.lastIndexOf(this) % 2 == 0` picks
//     BOLT for an even index and BEAM for an odd one (:136-143). Afterwards it
//     strictly alternates: lastMove(BEAM) -> BOLT, else BEAM (:145-149). `num` is
//     drawn by rollMove (AbstractMonster.java:465-467) and never read.
//     For the "3 Sentries" group (MonsterHelper.java:442-444) that gives the
//     opening telegraph Bolt / Beam / Bolt across slots 0, 1, 2.
//
// DRAW-COUNTING: sentry_init = one monster_hp_rng draw (HP) + one
// ai_rng.random(99) draw whose value is IGNORED. usePreBattleAction draws
// nothing. Each sentry_take_turn ends with exactly one more ignored
// ai_rng.random(99) draw -- ai_rng.counter advances by exactly 1 per turn.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// Construct the Sentry in slot `monster_index`: id, HP roll (monster_hp_rng),
// zeroed bookkeeping, and the position-keyed first decision (one ignored
// ai_rng.random(99) draw).
void sentry_init(CombatState& state, uint8_t monster_index) noexcept;

// Sentry.usePreBattleAction: queue the single Artifact stack. No RNG.
void sentry_use_pre_battle_action(CombatState& state,
                                  uint8_t monster_index) noexcept;

// One Sentry turn (Sentry.takeTurn): enqueue the decided move's effects then roll
// the next move (strict Bolt/Beam alternation). MonsterTurnFn-compatible.
void sentry_take_turn(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
