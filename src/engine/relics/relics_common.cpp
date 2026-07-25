// B3.24 common relics -- native hook bodies (moved verbatim out of
// relic_hooks.cpp's escape-hatch switch; see relic_native.hpp for the split's
// rationale). Parameters a body does not read are left unnamed to keep -Wextra
// quiet; the signature is the uniform RelicNativeFn.

#include "relics_common.hpp"

#include <cstdint>

#include "relic_native.hpp"             // heal_player
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/run_state.hpp"     // RelicSlot
#include "sts/engine/types.hpp"

namespace sts::engine {

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

// --- DEFERRED combat bodies --------------------------------------------------
//
// These six B3.24 commons are registered `native: true` with their hook
// inventory (so the row, the pool accounting and the acquisition-order wiring
// are all in place) but have NO combat body yet: each needs something the engine
// does not model at this point in the build order. They are DELIBERATELY EMPTY
// definitions, not omissions.
//
// Why a definition at all: the dispatch table is generated from
// registry/relics.yaml (STS_REGISTRY_NATIVE_RELICS, expanded in relic_hooks.cpp)
// and odr-uses a handler for every `native: true` row, so a relic whose body
// nobody wrote is an UNDEFINED REFERENCE at link time -- never a silent no-op.
// Deferral is therefore a decision that must be written down here, with its
// reason, instead of being indistinguishable from an oversight. Behaviour is
// identical to the old hand-written `case ...: default: return nullptr;` group:
// dispatch_native_relic_hook either skipped a null pointer or now calls a body
// that does nothing. Filling one in later is a local edit -- no table to touch.
//
// (BRONZE_SCALES / ODDLY_SMOOTH_STONE were un-deferred by the potion-support-
// powers follow-up: Thorns/Dexterity are registered, so both became DATA
// at_battle_start APPLY_POWER relics and never route here at all.)

// Akabeko.atBattleStart (Akabeko.java:31-35) -- addToTop ApplyPowerAction(
// VigorPower 8). DEFERRED: no Vigor row in registry/powers.yaml yet (B3.4 owns
// it), so the effect is unrepresentable.
void relic_native_akabeko(CombatState& /*s*/, RelicHook /*hook*/,
                          RelicSlot& /*slot*/,
                          const RelicHookContext& /*ctx*/) noexcept {}

// AncientTeaSet.atTurnStart (AncientTeaSet.java:50-61) -- if counter == -2, gain
// 2 energy on the first turn; onEnterRestRoom (:77-80) arms it. DEFERRED: the
// armed flag is cross-ROOM state owned by the run layer, not CombatState.
void relic_native_ancient_tea_set(CombatState& /*s*/, RelicHook /*hook*/,
                                  RelicSlot& /*slot*/,
                                  const RelicHookContext& /*ctx*/) noexcept {}

// ArtOfWar.onUseCard / atTurnStart (ArtOfWar.java:76-83, 64-74) -- +1 energy at
// turn start if no ATTACK was played last turn. DEFERRED: needs two independent
// flags (firstTurn + gainEnergyNext), i.e. multi-bit per-relic state beyond the
// single RelicSlot.counter.
void relic_native_art_of_war(CombatState& /*s*/, RelicHook /*hook*/,
                             RelicSlot& /*slot*/,
                             const RelicHookContext& /*ctx*/) noexcept {}

// Boot.onAttackToChangeDamage (Boot.java:30-38) -- if owner != null and the type
// is neither HP_LOSS nor THORNS and 0 < dmg < 5, return 5. DEFERRED: this is a
// DAMAGE-pipeline modifier, and the pipeline (interp.cpp op_damage) is
// float-exact and frozen; it is not a hook-queue effect.
void relic_native_boot(CombatState& /*s*/, RelicHook /*hook*/,
                       RelicSlot& /*slot*/,
                       const RelicHookContext& /*ctx*/) noexcept {}

// PreservedInsect.atBattleStart (PreservedInsect.java:31-39) -- in an ELITE
// room, set every monster's HP to 75%. DEFERRED: needs room-type context (absent
// from CombatState) plus a monster-HP scaling opcode.
void relic_native_preserved_insect(CombatState& /*s*/, RelicHook /*hook*/,
                                   RelicSlot& /*slot*/,
                                   const RelicHookContext& /*ctx*/) noexcept {}

// Toy Ornithopter -- heal 5 on potion use. NOT deferred for lack of modelling:
// it is dispatched on the RunState-owned potion route in run_advance (B4.4), not
// from a CombatState-only relic hook, so this combat-side entry stays empty by
// design.
void relic_native_toy_ornithopter(CombatState& /*s*/, RelicHook /*hook*/,
                                  RelicSlot& /*slot*/,
                                  const RelicHookContext& /*ctx*/) noexcept {}

}  // namespace sts::engine
