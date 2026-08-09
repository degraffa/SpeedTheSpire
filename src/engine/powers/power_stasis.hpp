#pragma once

// Stasis -- native power-hook body (registry/powers.yaml id 98,
// PowerId::STASIS).
//
// The Bronze Orb's stolen-card holder (StasisPower.java, 45 lines, read in
// full). Applied only by the APPLY_STASIS opcode's addToTop'd APPLY_POWER
// (ApplyStasisAction.java:77) at amount -1, with the stolen card's pool index
// riding the APPLY_POWER counter operand into the slot's `counter` as
// index + 1 (0 == "holding nothing", which no real apply ever writes -- the
// opcode only applies the power after a successful theft).
//
// ITS ONE COMBAT OVERRIDE is onDeath (:38-44): queue the give-back --
//
//     if (AbstractDungeon.player.hand.size() != 10)
//         addToBot(new MakeTempCardInHandAction(this.card, false, true));
//     else
//         addToBot(new MakeTempCardInDiscardAction(this.card, true));
//
// The HAND/DISCARD choice is a QUEUE-time read (this hook's moment); the hand
// arm re-checks the cap at RESOLVE and spills the overflow to the discard
// (MakeTempCardInHandAction.update:71-77). Both arms pass sameUUID == true
// (makeSameInstanceOf), so the engine moves the ORIGINAL pool row out of limbo
// rather than allocating a copy -- see Opcode::STASIS_RETURN (interp.hpp).
//
// This hook fires from BOTH death edges an orb can take: player damage, and
// the Bronze Automaton's post-super suicide sweep (SuicideAction 1-arg ctor ->
// die(true), BronzeAutomaton.java:182-187) -- killing the boss returns every
// stolen card while the victory queue drains, exactly as the game does.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_stasis(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept;

}  // namespace sts::engine
