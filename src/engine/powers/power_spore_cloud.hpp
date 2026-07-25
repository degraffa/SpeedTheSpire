#pragma once

// Spore Cloud (SporeCloudPower.java:14-42) -- the Fungi Beast's on-death
// release. When the beast dies it applies `amount` Vulnerable to the player
// (amount is 2, fixed by FungiBeast.java:52,77), UNLESS the battle is already
// ending.
//
// NATIVE, for two reasons a data hook program cannot express:
//
//   (1) THE BATTLE-ENDING GUARD. `if (AbstractDungeon.getCurrRoom()
//       .isBattleEnding()) return;` (:36-38). AbstractRoom.isBattleEnding
//       (AbstractRoom.java:628-635) is `isBattleOver ||
//       monsters.areMonstersBasicallyDead()`, and areMonstersBasicallyDead
//       (MonsterGroup.java:90-95) is "every monster isDying || isEscaping".
//       AbstractMonster.die sets isDying BEFORE walking the power list
//       (AbstractMonster.java:741-746), so the dying beast counts itself. With
//       two Fungi Beasts the FIRST death releases 2 Vulnerable and the SECOND
//       releases none -- kill order is observable, which is the whole point of
//       pinning the on-death trigger ordering.
//
//   (2) addToTop, NOT addToBot (:41). The Vulnerable jumps ahead of whatever the
//       killing card still had queued.
//
// The applied instance is `new VulnerablePower(player, amount, true)` -- the
// monster-sourced form every Act-1 monster Vulnerable uses, so it goes through
// the ordinary APPLY_POWER opcode and picks up the same Artifact interception
// and just-applied handling as the Red Slaver's Scrape or the Gremlin Nob's
// Skull Bash. The ApplyPowerAction's `source` is null here (:41) rather than the
// beast, which only matters for the source-side onApplyPower fan-out (Sadistic);
// the engine's APPLY_POWER carries the player as target and the dying monster as
// src, and no S1 monster holds Sadistic, so the two are indistinguishable today.

#include "power_native.hpp"

namespace sts::engine {

void power_native_spore_cloud(CombatState& s, Hook hook,
                              const HookContext& ctx) noexcept;

}  // namespace sts::engine
