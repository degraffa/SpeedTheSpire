#pragma once

// Time Warp -- native power-hook body (registry/powers.yaml id 110,
// PowerId::TIME_WARP).
//
// The Time Eater's card-count clock (TimeWarpPower.java, read in full). ONE
// hook, and it is the hook that did not exist before S2.2F:
//
//   * ON_AFTER_USE_CARD (:52-70)  ++amount; the 12th play zeroes the counter,
//                                 FORCES the player's turn to end, and gives
//                                 every monster +2 Strength.
//
// WHY ON_AFTER_USE_CARD (16) AND NOT ON_USE_CARD (1). They are different
// fan-outs at different moments with different participant lists:
//   * onUseCard fires from the UseCardAction CONSTRUCTOR (:20-45), BEFORE the
//     played card's own actions are queued, and walks player powers -> player
//     relics -> hand/discard/draw cards -> monster powers.
//   * onAfterUseCard fires from update() (:79-88), AFTER the card's program has
//     resolved, and walks PLAYER POWERS then MONSTER POWERS only. No relics, no
//     card-level stages.
// Counting on the wrong one would tick at the wrong moment (before the card's
// effects rather than after) and in the wrong order relative to the player's own
// powers. This is the binder the seam in interp_cards.cpp was reserved for.
//
// THE COUNTER IS `amount`, NOT PowerSlot.counter. TimeWarpPower.amount IS the
// displayed 0..11 number (:26 initialises it to 0), so the slot's second number
// stays unused. The power is NOT turn-based and nothing decays it: the count
// carries across turns, so a turn that plays 5 cards and a turn that plays 7
// together trip it.
//
// THE STRENGTH WALK IS DELIBERATELY UNGUARDED. `for (m : getMonsters().monsters)`
// (:66-67) has no liveness filter at all; safety comes from
// ApplyPowerAction.update's own isDeadOrEscaped early-out at RESOLVE time. So
// this body queues one APPLY_POWER per monster RECORD -- dead ones included --
// and lets the queued item no-op when it resolves. That is the same queue-time /
// resolve-time split the Donu and Deca protect loops turn on, and "fixing" it to
// a live-only walk would be observable the moment anything interleaves.
//
// THE FORCED TURN END IS EXECUTED INLINE, not queued, and that is exact.
// `callEndTurnEarlySequence()` (:63) is a SYNCHRONOUS call inside onAfterUseCard:
// it clears the card queue and arms the turn end BEFORE the loop below appends
// anything. Queueing END_PLAYER_TURN instead would let a card the player had
// already queued behind this one resolve first, which is the whole thing the
// Java's synchronous clear prevents. The opcode body is still the single door --
// this calls execute_opcode rather than re-implementing it.

#include "sts/engine/combat_state.hpp"
#include "sts/engine/power_hooks.hpp"  // Hook, HookContext

namespace sts::engine {

// TimeWarpPower.COUNTDOWN_AMT (:19) and STR_AMT (:18). Flat -- there is NO
// ascension branch anywhere in the class -- and named here because the tier-2
// tests count to them.
inline constexpr int32_t kTimeWarpCountdown = 12;
inline constexpr int32_t kTimeWarpStrength = 2;

void power_native_time_warp(CombatState& state, Hook hook,
                            const HookContext& ctx) noexcept;

}  // namespace sts::engine
