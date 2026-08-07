#pragma once

// The Shelled Parasite (the Act-2 encounters "Shell Parasite" and "Shelled
// Parasite and Fungi" -- note the FIRST key drops a letter the class name has;
// the class ID is "Shelled Parasite"). Stats and move effects are generated
// registry data (monsters.yaml id 29); move SELECTION is native.
//
// Provenance (D:\STS_BG_Mod\SlayTheSpireDecompiled), read in full:
//   ShelledParasite.java:39-205; PlatedArmorPower.java:11-74;
//   VampireDamageAction.java:18-46;
//   AbstractMonster.java:135-155 (ctor), 705-715 (init/rollMove),
//   765-775 (setHp), 384-399 (heal), 620-700 (damage), 431-491 (moveHistory).
//
// THE MOVE TREE (getMove, ShelledParasite.java:172-204), post-opener:
//   num < 20 : !lastMove(FELL)          ? FELL          : RECURSE(random(20,99))
//   num < 60 : !lastTwoMoves(D_STRIKE)  ? DOUBLE_STRIKE : LIFE_SUCK
//   else     : !lastTwoMoves(LIFE_SUCK) ? LIFE_SUCK     : DOUBLE_STRIKE
//
// FIVE THINGS WORTH KNOWING BEFORE READING THE BODY
//
// (1) getMove RECURSES, AND THE RECURSION COSTS A DRAW. When num < 20 and Fell
//     was already the last move, the Java calls `this.getMove(AbstractDungeon.
//     aiRng.random(20, 99))` (:191) -- a SECOND ai_rng draw, over a DIFFERENT
//     range (20..99 inclusive, not 0..99). The re-entry can only land in the
//     second or third arm, because 20..99 excludes the first, so the recursion
//     is exactly one level deep and cannot loop. That bound is a property of the
//     range, not a guard, which is why it is worth stating.
//
// (2) THE ARMOUR BREAK IS A POWER-REMOVAL EDGE, NOT A TURN. Plated Armor 14 is
//     applied pre-battle; PlatedArmorPower sheds a stack per real attack
//     (PlatedArmorPower.java:54-59) and, at zero, its removal fires
//     changeState("ARMOR_BREAK") (:151-159) -- setMove(STUNNED, Intent.STUN) plus
//     a createIntent repaint. THAT PATH ONLY EXISTS BECAUSE THE REMOVAL GOES
//     THROUGH THE SHARED CHOKE POINT: the landed Plated Armor body used to
//     decrement its own slot and zero power_id in place, which never reached
//     remove_slot_at, so the telegraph could not fire. It now queues a
//     REDUCE_POWER (which is also what the Java does), and the choke point
//     dispatches Hook::ON_POWER_REMOVED. See power_plated_armor.cpp.
//
// (3) THE STUNNED CASE setMoves AND *STILL* ROLLS -- the opposite of the Byrd's
//     HEADBUTT, and the contrast is load-bearing. Case 4 (:135-138) makes a
//     SYNCHRONOUS setMove(FELL, ATTACK_DEBUFF) and then falls out of the switch
//     into the trailing RollMoveAction at :140, which every case reaches. So the
//     setMove pushes move id 1 onto the history ring, and then the roll
//     IMMEDIATELY RE-DECIDES with `lastMove(FELL)` already true -- which is what
//     sends a num < 20 roll into the recursion above. The telegraphed Fell is
//     overwritten before the Parasite ever acts on it.
//
// (4) AI-RNG ACCOUNTING. One monster_hp_rng draw in the ctor (A7 column,
//     70..75). At A20 init() spends exactly ONE ai_rng draw: rollMove's
//     random(99), which the firstMove branch discards because the A17+ arm takes
//     Fell deterministically (:176-179). NO randomBoolean at A20 -- the coin at
//     :180 is the sub-A17 branch only, and getting that wrong would shift every
//     later draw. Afterwards one random(99) per turn, plus the recursion's
//     random(20,99) when it fires.
//
// (5) NO SPAWN PATH, AND damage() IS ANIMATION ONLY. ShelledParasite.java
//     declares usePreBattleAction, takeTurn, changeState, damage and getMove;
//     damage() (:163-170) is the standard hit animation past super.damage(), so
//     it needs no on_monster_damaged behaviour. Nothing splits or summons one.

#include <cstdint>

#include "sts/engine/combat_state.hpp"

namespace sts::engine {

// ShelledParasite.PLATED_ARMOR_AMT (ShelledParasite.java:54). The SAME 14 is
// both the Plated Armor stack and the pre-battle block gain (:106-107).
inline constexpr int32_t kShelledParasitePlatedArmor = 14;

void shelled_parasite_init(CombatState& state, uint8_t monster_index) noexcept;

// usePreBattleAction (ShelledParasite.java:104-108): PlatedArmorPower(14) THEN a
// direct GainBlockAction(14). No RNG draw.
void shelled_parasite_use_pre_battle_action(CombatState& state,
                                            uint8_t monster_index) noexcept;

// The trailing RollMoveAction (ShelledParasite.java:140), reached by ALL FOUR
// cases -- including STUNNED. May spend a second ai_rng draw (the recursion).
void shelled_parasite_roll_move(CombatState& state,
                                uint8_t monster_index) noexcept;

void shelled_parasite_take_turn(CombatState& state,
                                uint8_t monster_index) noexcept;

}  // namespace sts::engine
