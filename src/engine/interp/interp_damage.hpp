#pragma once

// DAMAGE-domain opcode bodies: the DamageInfo float pipeline (compute_damage,
// declared publicly in sts/engine/interp.hpp), the DAMAGE / LOSE_HP landing
// paths, and DROPKICK (a DAMAGE plus a conditional follow-up, so it lives with
// the op_damage it calls rather than with the card helpers). See interp_ops.hpp
// for the split's rationale.

#include <cstdint>

#include "sts/engine/combat_state.hpp"  // CombatState, ActionQueueItem
#include "sts/engine/interp.hpp"        // DamageType

namespace sts::engine {

// DAMAGE (see the definition for the full DamageInfo provenance). `pure` is
// the kDamagePure matrix bit (createDamageMatrix(amount, true): skip
// applyPowers, DamageInfo.java:126-136); `source_null` is the kDamageNullSource
// bit (a null-owner DamageInfo: the on-attacked/relic owner gates fail).
void op_damage(CombatState& s, uint8_t src, uint8_t tgt, int base,
               int strength_mult = 1,
               DamageType type = DamageType::NORMAL,
               bool pure = false, bool source_null = false) noexcept;

// DamageAction.update's ATTACKER-side cancel (DamageAction.java:69-73), read
// at RESOLVE time against the queued hit's OWNER (`src`). True == the hit is
// cancelled: the caller must drop the item entirely (no pipeline, no block
// decrement, no on-attacked fan-out, no HP write -- the Java's `isDone = true;
// return;` before `target.damage`). Called by execute_opcode's plain-DAMAGE
// dispatch ONLY: that opcode is the engine's model of a queued DamageAction,
// while the other op_damage callers model actions without this guard
// (DamageAllEnemiesAction, VampireDamageAction -- see the definition).
[[nodiscard]] bool damage_attacker_cancelled(const CombatState& s, uint8_t src,
                                             DamageType type,
                                             bool source_null) noexcept;

// LOSE_HP / LOSE_HP_PER_HAND.
void op_lose_hp(CombatState& s, uint8_t tgt, int amount) noexcept;

// DROPKICK.
void op_dropkick(CombatState& s, const ActionQueueItem& item) noexcept;

// DAMAGE_FEED (Feed): damage, then the max-HP gain when the hit left tgt dead.
void op_damage_feed(CombatState& s, uint8_t src, uint8_t tgt, int base,
                    int max_hp_gain) noexcept;

// DAMAGE_GREED (Hand of Greed): damage, then `gold` into CombatState.combat_gold
// when the hit left tgt dead. See the definition for the two structurally-inert
// terms of the Java gate.
void op_damage_greed(CombatState& s, uint8_t src, uint8_t tgt, int base,
                     int gold) noexcept;

// RITUAL_DAGGER (RitualDaggerAction): damage whose base is the played
// instance's misc, then `misc += increase` on that instance when the hit left
// tgt dead (the DAMAGE_GREED gate). `source_index` is the queue-time-stamped
// pool index of the played card (item.flags low byte).
void op_ritual_dagger(CombatState& s, uint8_t src, uint8_t tgt,
                      uint8_t source_index, int increase) noexcept;

// VAMPIRE_DAMAGE_ALL (Reaper): hit every live monster, then queue the summed heal.
void op_vampire_damage_all(CombatState& s, int base) noexcept;

// VAMPIRE_DAMAGE (the Shelled Parasite's Life Suck): hit ONE target, then heal the
// monster attacker by the HP that hit actually removed. See the definition for why
// the heal is applied inline rather than queued.
void op_vampire_damage(CombatState& s, uint8_t src, uint8_t tgt,
                       int base) noexcept;

// HEAL: the queued HealAction, routed through the onPlayerHeal relic seam.
void op_heal(CombatState& s, uint8_t tgt, int amount) noexcept;

}  // namespace sts::engine
