#pragma once

// Effect interpreter -- opcode dispatch + the DAMAGE pipeline (design doc §5.5,
// §6). The pump (action_queue.hpp) decides *which* queued ActionQueueItem
// resolves next; this layer decides *what an item does*. pump_step() calls
// execute_opcode() on every action_queue / pre_turn item it pops (the wiring
// lives in action_queue.cpp).
//
// Provenance (read from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * DamageInfo.applyPowers (DamageInfo.java:35-100) -- the exact hook order
//     for both ownership branches, float accumulation, single floor at the end
//     via MathUtils.floor, clamp >= 0.
//   * MathUtils.floor (MathUtils.java:217) -- (int)((double)v + 16384.0) - 16384,
//     replicated bit-for-bit as mathutils_floor() below (a true floor for the
//     skeleton's |value| < 16384 range; NOT round).
//   * StrengthPower.atDamageGive (StrengthPower.java:92-98) -- damage + amount
//     (float add), NORMAL only.
//   * VulnerablePower.atDamageReceive (VulnerablePower.java:61-73) -- damage *
//     1.5f, NORMAL only (the Odd Mushroom 1.25f / Paper Frog 1.75f relic
//     branches are unreachable in the relic-free skeleton).
//   * WeakPower.atDamageGive (WeakPower.java:61-70) -- damage * 0.75f, NORMAL
//     only (the Paper Crane 0.6f branch is unreachable here).
//   * AbstractCreature.addBlock -- in the base game only relic
//     (onPlayerGainedBlock) and power (onGainedBlock) hooks touch block gain;
//     Strength/Vulnerable/Weak do NOT. The skeleton has neither, so BLOCK is a
//     straight `block += amount` (hook site marked for later extension).
//
// -------------------------------------------------------------------------
// SCOPE-BOUNDARY NOTES:
//
// (1) SHUFFLE_IN's body -- the discard-pile-into-draw reshuffle via shuffle_rng
//     + the JDK LCG -- lives in piles.cpp (shuffle_discard_into_draw); the
//     dispatch delegates there.
//
// (2) ROLL_MOVE is a no-op. Jaw Worm rolls its next move DIRECTLY inside
//     jaw_worm_take_turn (the MonsterTurnFn callback), not via a queued item;
//     the opcode exists only for future data-driven monster AI expressed
//     through the registry (design doc §6).
//
// (3) Opcode NUMBERING: NOP == 0 is reserved as a safe no-op. This is required,
//     not cosmetic: value-initialized ActionQueueItems (opcode == 0) are pushed
//     through pump_step, and any unrecognized/zero opcode must drain harmlessly
//     rather than mis-fire a real effect. The real opcodes are therefore 1..8
//     (design doc §6's set, in its listed order); action_queue.hpp's
//     kOpcodeDrawCard mirrors Opcode::DRAW and is static_assert'd equal in
//     action_queue.cpp. Design doc §6 names the opcode SET but assigns no
//     numeric values, so the numbering is an implementation choice, not a doc
//     conflict.
//
// FIELD-ENCODING CONVENTIONS:
//   * Actor identity: src/tgt are actor indices -- monster slots 0..4, and the
//     player is kActorPlayer (== 0xFF, reused from action_queue.hpp). DAMAGE
//     reads src (attacker) to pick the ownership branch and iterate the
//     attacker's powers, and tgt (defender) for the receive hooks and to land
//     the damage.
//   * APPLY_POWER: ActionQueueItem has no dedicated power-id field, so `flags`
//     carries the PowerId (low 16 bits; make_apply_power_flags/
//     apply_power_id_from_flags below) and `amount` carries the stack amount.
//     `tgt` is the recipient actor.
//   * BLOCK: `tgt` is the recipient actor, `amount` the block gained.
//   * GAIN_ENERGY: always the player; `amount` is added to player_energy (no
//     max-energy field in CombatState -- Ironclad's base 3 is a constant, and
//     the skeleton has no relic/potion that stores a raised max, so no clamp).
//     SPENDING energy is the SAME opcode with a negative `amount` (player_energy
//     += negative) -- no separate opcode/field is needed, so the card-play
//     cost deduction reuses GAIN_ENERGY directly.
//   * DRAW: `amount` is the number of cards to draw (the start-of-turn
//     DrawCardAction(gameHandSize) carries 5). Player only. draw_cards
//     (piles.cpp) applies the up-front hand-size cap and empty-pile reshuffle.
//   * EXHAUST: `amount` carries the card-POOL index to exhaust (chosen over src
//     because src/tgt are actor lanes; a pool index 0..159 fits i32 trivially).
//     The card is located in `hand` and moved to the `exhaust` pile.

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // kActorPlayer (shared player-actor sentinel)
#include "sts/engine/combat_state.hpp"  // CombatState, ActionQueueItem, PowerSlot
#include "sts/engine/types.hpp"         // PowerId

namespace sts::engine {

// --- Opcode set (design doc §6) ---------------------------------------------
//
// NOP == 0 is reserved (see decision (3) above): unrecognized or value-init'd
// items dispatch here and do nothing. Real opcodes follow §6's listed order.
enum class Opcode : uint16_t {
    NOP = 0,          // reserved safe no-op (value-init / unrecognized opcode)
    DAMAGE = 1,       // src attacks tgt for `amount` base (full DamageInfo pipeline)
    BLOCK = 2,        // tgt gains `amount` block
    APPLY_POWER = 3,  // tgt gains PowerId(flags) x `amount` stacks
    DRAW = 4,         // player draws `amount` cards
    GAIN_ENERGY = 5,  // player_energy += `amount`
    SHUFFLE_IN = 6,   // reshuffle discard -> draw (implemented in piles.cpp)
    EXHAUST = 7,      // move card-pool-index `amount` from hand to exhaust
    ROLL_MOVE = 8,    // no-op: Jaw Worm rolls its own next move in its MonsterTurnFn
};

// --- APPLY_POWER field encoding ---------------------------------------------
// `flags` low 16 bits hold the PowerId; `amount` holds the stack count. Use
// these helpers so no caller hand-rolls the packing.
[[nodiscard]] constexpr uint32_t make_apply_power_flags(PowerId id) noexcept {
    return static_cast<uint32_t>(static_cast<uint16_t>(id));
}
[[nodiscard]] constexpr PowerId apply_power_id_from_flags(uint32_t flags) noexcept {
    return static_cast<PowerId>(static_cast<uint16_t>(flags & 0xFFFFu));
}

// --- libGDX MathUtils.floor (MathUtils.java:217) ----------------------------
// Replicated exactly so the DAMAGE floor matches the game bit-for-bit. A true
// floor for |value| < 16384 (always true for skeleton damage); NOT round.
[[nodiscard]] inline int mathutils_floor(float value) noexcept {
    return static_cast<int>(static_cast<double>(value) + 16384.0) - 16384;
}

// --- DAMAGE pipeline (pure) -------------------------------------------------
// Runs DamageInfo.applyPowers (design doc §5.5) for an attack from `src_actor`
// onto `tgt_actor` with `base_damage`, returning the floored, clamped(>=0)
// output WITHOUT applying it. Exposed pure so tier-2 tests can check the
// hand-computed damage number directly, free of block/hp interference; the
// DAMAGE opcode calls this and then lands the result on tgt. All accumulation
// is in float (trap 1: no integer shortcuts).
[[nodiscard]] int compute_damage(const CombatState& state, uint8_t src_actor,
                                 uint8_t tgt_actor, int base_damage) noexcept;

// --- Dispatch ----------------------------------------------------------------
// Execute one popped ActionQueueItem against `state`. One case per Opcode;
// NOP and any unrecognized opcode are safe no-ops (see decision (3)). This is
// what pump_step() invokes on every action_queue / pre_turn item it pops.
void execute_opcode(CombatState& state, const ActionQueueItem& item) noexcept;

}  // namespace sts::engine
