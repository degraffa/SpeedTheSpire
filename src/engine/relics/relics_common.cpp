// COMMON-tier relics -- native hook bodies (moved verbatim out of
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
        heal_player_with_relics(s, 2);
    }
}

void relic_native_centennial_puzzle(CombatState& s, RelicHook hook,
                                    RelicSlot& /*slot*/,
                                    const RelicHookContext& /*ctx*/) noexcept {
    // CentennialPuzzle.wasHPLost (CentennialPuzzle.java:40-49): the FIRST HP
    // loss of a combat addToTop(DrawCardAction(player, NUM_CARDS)) with
    // NUM_CARDS == 3 (:20, :44).
    //
    // The once-per-combat gate is `private static boolean usedThisCombat`
    // (:21) -- combat-global, read+set at :41/:46, reset by atPreBattle (:34).
    // It is deliberately NOT this relic's `slot.counter`: nothing in
    // CentennialPuzzle.java ever writes `this.counter`, so the counter stays at
    // AbstractRelic's -1 for the whole run and any write here folds out to
    // RunState and diverges from the capture. The latch lives in
    // kCombatFlagCentennialPuzzleUsed (combat_state.hpp), whose per-combat reset
    // is enter_combat's fresh `CombatState s{}` -- see that constant for the
    // full derivation.
    //
    // The Java also gates on getCurrRoom().phase == RoomPhase.COMBAT (:41); a
    // CombatState only exists inside a combat, and dispatch_relics_was_hp_lost
    // is reached solely from the in-combat damage path (power_hooks.cpp), so
    // that clause is structurally true at every call site here. The
    // damageAmount > 0 half of the same condition is enforced by
    // dispatch_relics_was_hp_lost (relic_hooks.cpp).
    if (hook == RelicHook::WAS_HP_LOST &&
        (s.flags & kCombatFlagCentennialPuzzleUsed) == 0u) {
        s.flags |= kCombatFlagCentennialPuzzleUsed;  // usedThisCombat = true (:46)
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
    // damage) then the counter resets. The double-damage PenNibPower has no
    // registry/powers.yaml row, so the empowerment is DEFERRED; the COUNTER is
    // live here so the accounting is already correct when the power lands.
    // counter persists in the RelicSlot (design §4.3).
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
    // slot.counter is the suppression latch: 1 while the grant must not fire
    // (isActive, OR the combat was entered already bloodied), 0 while armed.
    //
    // AT_BATTLE_START seeds it from starting HP, reproducing two Java facts at
    // once: preBattlePrep pre-seeds isBloodied = currentHealth <= maxHealth / 2
    // (AbstractPlayer.java:1575), so a combat ENTERED at or below half HP never
    // fires the damage-side onBloodied cross (AbstractPlayer.java:1476-1481 --
    // it fires only when isBloodied flips false->true); and RedSkull.
    // atBattleStart resets isActive = false (RedSkull.java:37), so a latch a
    // previous combat left behind (fold_back_combat persists the counter into
    // the run slot) re-arms here. The int-division seed and the float damage
    // cross agree for integer HP: cur <= max/2  ==  cur*2 <= max.
    //
    // NOT MODELLED, deliberately: the action atBattleStart QUEUES at
    // RedSkull.java:38 is an unavailable anonymous inner class in this
    // decompiled tree (it reads/writes isActive per the synthetic
    // access$000/002, :76-83) -- its body is not evidence-derivable and stays
    // a recorded deferral rather than an invention.
    if (hook == RelicHook::AT_BATTLE_START) {
        slot.counter =
            (static_cast<int32_t>(s.player_hp) * 2 <= s.player_max_hp) ? 1 : 0;
        return;
    }
    // RedSkull.onBloodied (RedSkull.java:41-52, routed through wasHPLost):
    // when the HP loss drops the player to <=50% max HP while the latch is
    // clear, gain 3 Strength (addToTop ApplyPowerAction(StrengthPower 3),
    // :47) and latch (isActive = true, :49). The onNotBloodied -3 on healing
    // back over 50% (:54-63) is DEFERRED (needs a heal-cross hook). Strength
    // IS registered (id 1).
    if (hook == RelicHook::WAS_HP_LOST && slot.counter == 0 &&
        static_cast<int32_t>(s.player_hp) * 2 <= s.player_max_hp) {
        slot.counter = 1;
        ActionQueueItem gain{};
        gain.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        gain.src = kActorPlayer;
        gain.tgt = kActorPlayer;
        gain.amount = 3;
        gain.flags = make_apply_power_flags(PowerId::STRENGTH);
        add_to_top(s, gain);  // addToTop (RedSkull.java:47)
    }
}

// --- DEFERRED combat bodies --------------------------------------------------
//
// These six commons are registered `native: true` with their hook
// inventory (so the row, the pool accounting and the acquisition-order wiring
// are all in place) but have NO combat body. Five are genuinely DEFERRED, each
// needing something the engine does not model; the sixth (Toy Ornithopter) is
// live on the run-level potion route instead. They are DELIBERATELY EMPTY
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
// VigorPower 8). DEFERRED: registry/powers.yaml has no VIGOR row, so the power
// this relic applies cannot be named -- the effect is unrepresentable.
void relic_native_akabeko(CombatState& /*s*/, RelicHook /*hook*/,
                          RelicSlot& /*slot*/,
                          const RelicHookContext& /*ctx*/) noexcept {}

// AncientTeaSet.atTurnStart (AncientTeaSet.java:50-61) -- if counter == -2, gain
// 2 energy on the first turn; onEnterRestRoom (:77-80) arms it. DEFERRED
// because there is nowhere to ARM it from: rest rooms are not implemented
// (entering one parks at ROOM_UNIMPLEMENTED, run_advance.cpp), so
// onEnterRestRoom never fires. NOTE: the armed flag itself is no longer the
// obstacle -- RelicSlot.counter survives across rooms, because fold_back_combat
// copies each combat counter back into RunState.relics at combat end.
void relic_native_ancient_tea_set(CombatState& /*s*/, RelicHook /*hook*/,
                                  RelicSlot& /*slot*/,
                                  const RelicHookContext& /*ctx*/) noexcept {}

// ArtOfWar.onUseCard / atTurnStart (ArtOfWar.java:76-83, 64-74) -- +1 energy at
// turn start if no ATTACK was played last turn. DEFERRED: it needs two
// independent per-relic flags (firstTurn + gainEnergyNext). RelicSlot.counter
// is a signed 16-bit field and could carry both as bits, so this is a missing
// DECISION about how a relic encodes multi-flag state, not missing storage.
void relic_native_art_of_war(CombatState& /*s*/, RelicHook /*hook*/,
                             RelicSlot& /*slot*/,
                             const RelicHookContext& /*ctx*/) noexcept {}

// Boot.onAttackToChangeDamage (Boot.java:30-38) -- if owner != null and the type
// is neither HP_LOSS nor THORNS and 0 < dmg < 5, return 5. DEFERRED: this is a
// DAMAGE-pipeline modifier, and the pipeline (interp/interp_damage.cpp
// op_damage) is float-exact and frozen; it is not a hook-queue effect.
void relic_native_boot(CombatState& /*s*/, RelicHook /*hook*/,
                       RelicSlot& /*slot*/,
                       const RelicHookContext& /*ctx*/) noexcept {}

// PreservedInsect.atBattleStart (PreservedInsect.java:31-39) -- in an ELITE
// room, set every monster's HP to 75%. DEFERRED: a relic hook is handed only a
// CombatState, and CombatState carries no room kind -- so the body cannot tell
// an elite fight from an ordinary one. (Elite ROOMS do exist: RunController
// tracks room_type, and run_advance.cpp enters elite combats. Discharging this
// needs that fact plumbed into the combat hook, plus a monster-HP scaling
// opcode.)
void relic_native_preserved_insect(CombatState& /*s*/, RelicHook /*hook*/,
                                   RelicSlot& /*slot*/,
                                   const RelicHookContext& /*ctx*/) noexcept {}

// Toy Ornithopter -- heal 5 on potion use. NOT deferred and NOT missing: it is
// LIVE, dispatched on the RunState-owned potion route
// (dispatch_run_relics_on_use_potion, run_advance.cpp) rather than from a
// CombatState-only relic hook, so this combat-side entry is empty by design.
void relic_native_toy_ornithopter(CombatState& /*s*/, RelicHook /*hook*/,
                                  RelicSlot& /*slot*/,
                                  const RelicHookContext& /*ctx*/) noexcept {}

}  // namespace sts::engine
