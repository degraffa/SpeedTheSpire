#pragma once

// The Fungi Beast (MonsterHelper.getEncounter "2 Fungi Beasts",
// MonsterHelper.java:427-429; also one arm of the Exordium Wildlife group's
// bottomGetStrongWildlife, :785-822). HP range and move effects are generated
// registry data; move SELECTION is native, per design doc B §4.2's budget. The
// fixed S1 difficulty is A20, so the body follows the cited A17/A7/A2 branches
// while still resolving its numbers from the table columns.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   FungiBeast.java:29-134; SporeCloudPower.java:14-42;
//   AbstractMonster.java:431-491,705-715,765-775,925-937.
//
// THREE THINGS WORTH KNOWING BEFORE READING THE BODIES
//
// (1) IT ROLLS EVERY TURN AND READS THE ROLL. takeTurn ends in a QUEUED
//     RollMoveAction (FungiBeast.java:97) sitting AFTER the switch, so both move
//     bodies reach it. Draw accounting at A20: one monster_hp_rng draw per ctor
//     (setHp), one ai_rng.random(99) at init(), and one more per turn at DEQUEUE
//     time. getMove (:100-113) compares that `num` against 60, so the value
//     selects the move rather than being discarded.
//
// (2) SPORE CLOUD IS A PRE-BATTLE POWER WITH AN ON-DEATH BODY. usePreBattleAction
//     (:75-78) applies SporeCloudPower(this, 2) to ITSELF -- a fixed 2 with no
//     ascension branch (VULN_AMT, :52), which is why it is queued here rather
//     than carried as a registry tier column (the GremlinWarrior/Angry
//     precedent). What the power then does on the beast's death is the first
//     consumer of Hook::ON_DEATH; see src/engine/powers/power_spore_cloud.cpp,
//     and note the ordering rule it turns on: with two beasts, killing the first
//     releases 2 Vulnerable and killing the LAST releases none.
//
// (3) ITS damage() OVERRIDE IS ANIMATION ONLY. FungiBeast.damage (:126-133)
//     wraps super.damage() and then, for a non-THORNS hit with output > 0, plays
//     the "Hit" spine animation. Nothing there touches combat state or draws RNG,
//     so an empty on_monster_damaged case is the complete translation -- spelled
//     as a case rather than left to the `default:` so the omission is checkable
//     (the Sentry precedent, monster_dispatch.cpp).

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

void fungi_beast_init(CombatState& state, uint8_t monster_index) noexcept;
void fungi_beast_take_turn(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (FungiBeast.java:75-78): addToBottom ApplyPowerAction(this,
// this, new SporeCloudPower(this, 2)). Draws no RNG.
void fungi_beast_use_pre_battle_action(CombatState& state,
                                       uint8_t monster_index) noexcept;

// The queued RollMoveAction body (FungiBeast.java:97): one ai_rng.random(99)
// followed by getMove's d60 / move-history tree.
void fungi_beast_roll_move(CombatState& state, uint8_t monster_index) noexcept;

}  // namespace sts::engine
