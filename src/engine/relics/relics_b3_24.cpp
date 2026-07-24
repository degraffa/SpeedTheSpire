// B3.24 starter + common relics -- native hook bodies (moved verbatim out of
// relic_hooks.cpp's escape-hatch switch; see relic_native.hpp for the split's
// rationale). Parameters a body does not read are left unnamed to keep -Wextra
// quiet; the signature is the uniform RelicNativeFn.

#include "relics_b3_24.hpp"

#include <cstdint>

#include "relic_native.hpp"             // heal_player
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/run_state.hpp"     // RelicSlot
#include "sts/engine/types.hpp"

namespace sts::engine {

void relic_native_burning_blood(CombatState& s, RelicHook hook,
                                RelicSlot& /*slot*/,
                                const RelicHookContext& /*ctx*/) noexcept {
    // BurningBlood.onVictory: heal 6 at combat end (clamped to max HP).
    if (hook == RelicHook::ON_VICTORY) {
        heal_player(s, 6);
    }
}

void relic_native_blood_vial(CombatState& s, RelicHook hook,
                             RelicSlot& /*slot*/,
                             const RelicHookContext& /*ctx*/) noexcept {
    // BloodVial.atBattleStart: heal 2 (clamped).
    if (hook == RelicHook::AT_BATTLE_START) {
        heal_player(s, 2);
    }
}

void relic_native_centennial_puzzle(CombatState& s, RelicHook hook,
                                    RelicSlot& slot,
                                    const RelicHookContext& /*ctx*/) noexcept {
    // CentennialPuzzle.wasHPLost: the FIRST HP loss in a combat draws 3.
    // slot.counter is the once-per-combat flag (0 = not yet fired).
    if (hook == RelicHook::WAS_HP_LOST && slot.counter == 0) {
        slot.counter = 1;
        ActionQueueItem draw{};
        draw.opcode = static_cast<uint16_t>(Opcode::DRAW);
        draw.src = kActorPlayer;
        draw.tgt = kActorPlayer;
        draw.amount = 3;
        add_to_top(s, draw);  // addToTop (CentennialPuzzle.java:44)
    }
}

void relic_native_orichalcum(CombatState& s, RelicHook hook,
                             RelicSlot& /*slot*/,
                             const RelicHookContext& /*ctx*/) noexcept {
    // Orichalcum.onPlayerEndTurn: if the player has 0 block, gain 6.
    if (hook == RelicHook::ON_PLAYER_END_TURN && s.player_block == 0) {
        ActionQueueItem blk{};
        blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
        blk.src = kActorPlayer;
        blk.tgt = kActorPlayer;
        blk.amount = 6;
        blk.flags = kBlockNoPowers;  // direct GainBlockAction -- no Dexterity
        add_to_top(s, blk);  // addToTop (Orichalcum.java:38)
    }
}

void relic_native_nunchaku(CombatState& s, RelicHook hook, RelicSlot& slot,
                           const RelicHookContext& ctx) noexcept {
    // Nunchaku.onUseCard: every 10th ATTACK played grants 1 energy. The
    // counter persists in the RelicSlot (stage-a §4.3's {relic_id, counter}).
    if (hook == RelicHook::ON_USE_CARD && ctx.card_is_attack) {
        ++slot.counter;
        if (slot.counter % 10 == 0) {
            slot.counter = 0;
            ActionQueueItem e{};
            e.opcode = static_cast<uint16_t>(Opcode::GAIN_ENERGY);
            e.src = kActorPlayer;
            e.tgt = kActorPlayer;
            e.amount = 1;
            add_to_bottom(s, e);  // addToBot (Nunchaku.java:48)
        }
    }
}

void relic_native_pen_nib(CombatState& /*s*/, RelicHook hook, RelicSlot& slot,
                          const RelicHookContext& ctx) noexcept {
    // PenNib.onUseCard: counts ATTACKs; the 10th is empowered (double
    // damage) then the counter resets. The double-damage PenNib power is
    // DEFERRED (not yet in powers.yaml, B3.4); the COUNTER is live here so
    // the accounting is correct when the power lands. counter persists in
    // the RelicSlot (stage-a §4.3).
    if (hook == RelicHook::ON_USE_CARD && ctx.card_is_attack) {
        ++slot.counter;
        if (slot.counter >= 10) {
            slot.counter = 0;  // PenNib.java:44-47 (empowerment: DEFERRED)
        }
    }
}

void relic_native_happy_flower(CombatState& s, RelicHook hook, RelicSlot& slot,
                               const RelicHookContext& /*ctx*/) noexcept {
    // HappyFlower.atTurnStart: every 3rd turn-start grants 1 energy. counter
    // persists in the RelicSlot. (The first-turn +2 quirk -- counter starts
    // at AbstractRelic's -1 -- is DEFERRED; the 3-turn cadence is live.)
    if (hook == RelicHook::AT_TURN_START) {
        ++slot.counter;
        if (slot.counter >= 3) {
            slot.counter = 0;
            ActionQueueItem e{};
            e.opcode = static_cast<uint16_t>(Opcode::GAIN_ENERGY);
            e.src = kActorPlayer;
            e.tgt = kActorPlayer;
            e.amount = 1;
            add_to_bottom(s, e);  // addToBot (HappyFlower.java:60)
        }
    }
}

void relic_native_lantern(CombatState& s, RelicHook hook, RelicSlot& slot,
                          const RelicHookContext& /*ctx*/) noexcept {
    // Lantern.atTurnStart: +1 energy on the FIRST turn only. slot.counter is
    // the fired-flag (0 = not yet).
    if (hook == RelicHook::AT_TURN_START && slot.counter == 0) {
        slot.counter = 1;
        ActionQueueItem e{};
        e.opcode = static_cast<uint16_t>(Opcode::GAIN_ENERGY);
        e.src = kActorPlayer;
        e.tgt = kActorPlayer;
        e.amount = 1;
        add_to_top(s, e);  // addToTop (Lantern.java:59)
    }
}

void relic_native_red_skull(CombatState& s, RelicHook hook, RelicSlot& slot,
                            const RelicHookContext& /*ctx*/) noexcept {
    // RedSkull.onBloodied (routed through wasHPLost): when the HP loss drops
    // the player to <=50% max HP and Red Skull is not already active, gain 3
    // Strength. slot.counter is the isActive flag (0 = inactive). The
    // onNotBloodied -3 on healing back over 50% is DEFERRED (needs a
    // heal-cross hook). Strength IS registered (id 1).
    if (hook == RelicHook::WAS_HP_LOST && slot.counter == 0 &&
        static_cast<int32_t>(s.player_hp) * 2 <= s.player_max_hp) {
        slot.counter = 1;
        ActionQueueItem gain{};
        gain.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        gain.src = kActorPlayer;
        gain.tgt = kActorPlayer;
        gain.amount = 3;
        gain.flags = make_apply_power_flags(PowerId::STRENGTH);
        add_to_top(s, gain);  // addToTop (RedSkull.java:54)
    }
}

}  // namespace sts::engine
