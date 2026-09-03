#pragma once

// Beat of Death -- native power-hook body (registry/powers.yaml id 138,
// PowerId::BEAT_OF_DEATH). The Corrupt Heart's per-card pulse
// (BeatOfDeathPower.java, 51 lines, read in full).
//
// ONE HOOK. onAfterUseCard (:39-44):
//     flash();
//     addToBot(new DamageAction(AbstractDungeon.player,
//                               new DamageInfo(this.owner, this.amount,
//                                              DamageInfo.DamageType.THORNS),
//                               AttackEffect.BLUNT_LIGHT));
//     updateDescription();
// -- ONE THORNS hit at the power's own amount, PER CARD PLAYED, with NO card
// filter of any kind in the body. `flash` and `updateDescription` are
// presentation.
//
// THE HOOK IS ON_AFTER_USE_CARD (16), NOT ON_USE_CARD (1), and the two genuinely
// differ (UseCardAction.java:20-45 vs :55-64,:79-88): onUseCard fires from the
// UseCardAction CONSTRUCTOR, before the played card's own actions are queued,
// and walks player powers -> player relics -> hand/discard/draw cards ->
// monster powers; onAfterUseCard fires from update(), AFTER the card's program
// has resolved, and walks PLAYER POWERS then MONSTER POWERS only. So the pulse
// lands BEHIND everything the card did, not in front of it -- which is exactly
// why the lethal card's pulse is queued behind the kill.
//
// THE THORNS TYPE IS LOAD-BEARING TWICE OVER.
//   * It skips atDamageGive / atDamageReceive, so neither the Heart's Strength
//     ramp nor the player's Vulnerable moves the number (block still absorbs
//     it) -- op_damage's NORMAL-only pipeline gate (interp/interp_damage.cpp).
//   * PainfulStabsPower.onInflictDamage EXCLUDES THORNS
//     (PainfulStabsPower.java:40-44), so the Heart's own Painful Stabs -- its
//     buffCount == 2 rung (CorruptHeart.java:138) -- is NOT triggered by this
//     pulse. Only BLOOD_SHOTS' landed hits pay Wounds.
//
// STACKING IS LIVE, unlike on most markers: stackPower is not overridden, so it
// is additive, and the Heart applies a SECOND BeatOfDeathPower(this, 1) from its
// buff ladder (:134) on top of the pre-battle 1 (2 at A19+, :97-102).
//
// It also reaches the terminal-adjudication seam S3.44 closed: a pulse is queued
// after EVERY card including the one that kills the Heart, so a lethal turn
// leaves a queued THORNS item whose owner is already dead. S3.44's resolver
// drains what is queued rather than a snapshot, and S2.49's attacker-side cancel
// exempts THORNS, so the pulse still lands.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

void power_native_beat_of_death(CombatState& state, Hook hook,
                                const HookContext& ctx) noexcept;

}  // namespace sts::engine
