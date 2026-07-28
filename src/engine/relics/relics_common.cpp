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

namespace {

// The one action both PenNib call sites queue: addToBot ApplyPowerAction(player,
// player, PenNibPower(player, 1), 1, true) (PenNib.java:51 and :60). Spelled
// once so the two sites cannot drift.
void queue_pen_nib_power(CombatState& s) noexcept {
    ActionQueueItem gain{};
    gain.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    gain.src = kActorPlayer;
    gain.tgt = kActorPlayer;
    gain.amount = 1;
    gain.flags = make_apply_power_flags(PowerId::PEN_NIB);
    add_to_bottom(s, gain);
}

}  // namespace

void relic_native_pen_nib(CombatState& s, RelicHook hook, RelicSlot& slot,
                          const RelicHookContext& ctx) noexcept {
    // PenNib (PenNib.java:36-62; the ledger and this row previously cited
    // :40-56 / :44-47, both wrong). `counter` starts at 0 (the ctor, :28) and is
    // RUN-persistent -- fold_back_combat copies it into RunState.relics.
    //
    // onUseCard (:36-52):
    //     if (card.type == ATTACK) {
    //         ++counter;
    //         if (counter == 10) { counter = 0; flash(); pulse = false; }
    //         else if (counter == 9) { beginPulse(); pulse = true;
    //             hand.refreshHandLayout();
    //             addToBot(RelicAboveCreatureAction);            -- cosmetic
    //             addToBot(ApplyPowerAction(p, p, PenNibPower(p, 1), 1, true)); }
    //     }
    //
    // The 9-vs-10 asymmetry is the whole mechanic and is easy to get backwards:
    // the power is granted AFTER the NINTH attack, so the TENTH attack is the
    // empowered one, and reaching ten RESETS the counter without granting. The
    // 5-arg ApplyPowerAction's trailing boolean is a speed flag, not a semantic
    // one. `pulse` and refreshHandLayout are presentation.
    //
    // atBattleStart (:54-62): `if (counter == 9)` re-grant, WITHOUT touching the
    // counter. Reachable precisely because the counter survives the combat that
    // set it to 9 -- a fresh combat can therefore open with the power already up.
    if (hook == RelicHook::AT_BATTLE_START) {
        if (slot.counter == 9) {
            queue_pen_nib_power(s);  // addToBot (PenNib.java:60)
        }
        return;
    }
    if (hook == RelicHook::ON_USE_CARD && ctx.card_is_attack) {
        ++slot.counter;
        if (slot.counter == 10) {
            slot.counter = 0;  // (:40-43) -- reset, no grant
        } else if (slot.counter == 9) {
            queue_pen_nib_power(s);  // addToBot (PenNib.java:51)
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
// The commons below are registered `native: true` with their hook inventory (so
// the row, the pool accounting and the acquisition-order wiring are all in
// place) but have NO combat body. Toy Ornithopter is live on the run-level
// potion route instead; anything else still here is genuinely DEFERRED and says
// why. They are DELIBERATELY EMPTY definitions, not omissions.
//
// Bodies that have LEFT this block keep their implementation where the empty
// definition stood, so the citation and the code stay together.
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

void relic_native_akabeko(CombatState& s, RelicHook hook, RelicSlot& /*slot*/,
                          const RelicHookContext& /*ctx*/) noexcept {
    // Akabeko.atBattleStart (Akabeko.java:30-35):
    //     flash();
    //     addToTop(ApplyPowerAction(player, player, VigorPower(player, 8), 8));
    //     addToTop(RelicAboveCreatureAction(player, this));   -- cosmetic
    //
    // Unconditional -- no room, HP or deck gate of any kind. VIGOR = 8 (:19).
    // The two addToTop pushes reverse; with the cosmetic dropped, one add_to_top
    // is the whole body.
    if (hook != RelicHook::AT_BATTLE_START) {
        return;
    }
    ActionQueueItem gain{};
    gain.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    gain.src = kActorPlayer;
    gain.tgt = kActorPlayer;
    gain.amount = 8;
    gain.flags = make_apply_power_flags(PowerId::VIGOR);
    add_to_top(s, gain);  // addToTop (Akabeko.java:33)
}

void relic_native_ancient_tea_set(CombatState& s, RelicHook hook,
                                  RelicSlot& slot,
                                  const RelicHookContext& /*ctx*/) noexcept {
    // AncientTeaSet.atTurnStart (AncientTeaSet.java:49-61):
    //     if (this.firstTurn) {
    //         if (this.counter == -2) {
    //             pulse = false; this.counter = -1; flash();
    //             addToTop(new GainEnergyAction(2));
    //             addToTop(RelicAboveCreatureAction);   -- cosmetic
    //         }
    //         this.firstTurn = false;
    //     }
    // atPreBattle (:63-66) sets firstTurn = true; onEnterRestRoom (:76-81) sets
    // counter = -2, which is the ARMING half and lives at the run layer.
    //
    // RelicSlot.counter IS the cross-room state, exactly: -2 armed, -1 spent,
    // and fold_back_combat already carries it between rooms. That is the GAME'S
    // OWN encoding -- AncientTeaSet writes this.counter directly and
    // CommunicationMod reports it -- so unlike Centennial Puzzle / Orange Pellets
    // / Art of War there is nothing to move out of the counter here. The earlier
    // note claiming this relic needed "state beyond RelicSlot.counter" was wrong;
    // so was its successor claiming rest rooms do not exist, which they have
    // since.
    //
    // `firstTurn` needs no storage. This hook runs from start_of_turn BEFORE
    // `++s.turn` (action_queue.cpp) and combat construction leaves s.turn == 0,
    // so the first atTurnStart of a combat is exactly `s.turn == 0`. A skipped
    // turn cannot re-arm it mid-combat, which is the property the Java's one-shot
    // boolean has.
    //
    // The +2 is addToTop while turn 1's own energy comes from an ADD onto a
    // just-zeroed panel (GainEnergyAndEnableControlsAction, AbstractRoom.java:241
    // -> EnergyPanel.addEnergy, EnergyPanel.java:59-66), so both are additive and
    // the ordering between them is harmless.
    if (hook != RelicHook::AT_TURN_START || s.turn != 0 || slot.counter != -2) {
        return;
    }
    slot.counter = -1;  // (:54)
    ActionQueueItem e{};
    e.opcode = static_cast<uint16_t>(Opcode::GAIN_ENERGY);
    e.src = kActorPlayer;
    e.tgt = kActorPlayer;
    e.amount = 2;  // ENERGY_AMT (AncientTeaSet.java:22)
    add_to_top(s, e);  // addToTop (:56)
}

void relic_native_art_of_war(CombatState& s, RelicHook hook,
                             RelicSlot& /*slot*/,
                             const RelicHookContext& ctx) noexcept {
    // ArtOfWar (ArtOfWar.java:52-82):
    //     atPreBattle (:52-61) : firstTurn = true; gainEnergyNext = true;
    //     atTurnStart (:63-74) : if (gainEnergyNext && !firstTurn) {
    //                                addToBot(RelicAboveCreatureAction);  -- cosmetic
    //                                addToBot(new GainEnergyAction(1)); }
    //                            firstTurn = false;
    //                            gainEnergyNext = true;
    //     onUseCard   (:76-82) : if (card.type == ATTACK) gainEnergyNext = false;
    //
    // Net: +1 energy at the start of turn N (N >= 2) iff no ATTACK was played
    // during turn N-1.
    //
    // ONE bit of storage, not two. `firstTurn` is `s.turn == 0` (this hook runs
    // before start_of_turn's ++s.turn), and `gainEnergyNext` is stored INVERTED
    // as kCombatFlagArtOfWarAttackPlayed so the value-initialised default is
    // already atPreBattle's `gainEnergyNext = true`. See that constant for why a
    // flags bit rather than RelicSlot.counter.
    //
    // THE LINE ORDER IS LOAD-BEARING: `gainEnergyNext = true` is the LAST
    // statement of atTurnStart, AFTER the check. Clearing the latch before the
    // test would grant every turn.
    if (hook == RelicHook::ON_USE_CARD) {
        if (ctx.card_is_attack) {
            s.flags |= kCombatFlagArtOfWarAttackPlayed;  // gainEnergyNext = false
        }
        return;
    }
    if (hook != RelicHook::AT_TURN_START) {
        return;
    }
    if (s.turn != 0 && (s.flags & kCombatFlagArtOfWarAttackPlayed) == 0u) {
        ActionQueueItem e{};
        e.opcode = static_cast<uint16_t>(Opcode::GAIN_ENERGY);
        e.src = kActorPlayer;
        e.tgt = kActorPlayer;
        e.amount = 1;
        add_to_bottom(s, e);  // addToBot (ArtOfWar.java:71)
    }
    s.flags &= ~kCombatFlagArtOfWarAttackPlayed;  // gainEnergyNext = true (:73)
}

// Boot.onAttackToChangeDamage (Boot.java:30-38) -- LIVE, but NOT here.
//
// The body lives at `apply_boot` in interp/interp_damage.cpp, inside op_damage's
// INTEGER TAIL, immediately after the block subtraction and before Buffer /
// Thorns / Torii / Tungsten Rod -- exactly where AbstractMonster.damage:639-643
// and AbstractPlayer.damage:1399-1403 run the player's relics'
// onAttackToChangeDamage. Read that function for the ordering derivation and for
// why the number Boot sees is the UNBLOCKED residue rather than the pre-block
// output. The frozen float pipeline (compute_damage) is untouched: this is an
// integer step, and it sits beside two relic modifiers that were already there.
//
// This definition stays EMPTY and stays here on purpose. The generated dispatch
// odr-uses a handler for every `native: true` row, so deleting it is a link
// error; and the row's `on_attack: []` binding is documentation of WHICH Java
// hook the bespoke site reproduces. RelicHook::ON_ATTACK (value 12) remains
// allocated and unfired -- it has no dispatcher, and RelicHookContext carries no
// damage in/out channel, so wiring one for a single relic would be a far larger
// change than the eight-line helper. Magic Flower, Torii and Tungsten Rod are the
// same shape.
void relic_native_boot(CombatState& /*s*/, RelicHook /*hook*/,
                       RelicSlot& /*slot*/,
                       const RelicHookContext& /*ctx*/) noexcept {}

void relic_native_preserved_insect(CombatState& s, RelicHook hook,
                                   RelicSlot& /*slot*/,
                                   const RelicHookContext& /*ctx*/) noexcept {
    // PreservedInsect.atBattleStart (PreservedInsect.java:30-41):
    //     if (getCurrRoom().eliteTrigger) {
    //         flash();
    //         for (m : getCurrRoom().monsters.monsters) {
    //             if (m.currentHealth <= (int)((float)m.maxHealth * (1.0f - 0.25f))) continue;
    //             m.currentHealth = (int)((float)m.maxHealth * (1.0f - 0.25f));
    //             m.healthBarUpdatedEvent();
    //         }
    //         addToTop(RelicAboveCreatureAction(player, this));  -- cosmetic
    //     }
    //
    // This is a CURRENT-health CLAMP, not an HP-init change: maxHealth is
    // untouched, so every max-HP-relative gate downstream (the large slimes'
    // split at maxHealth/2, Lagavulin's wake, health-bar denominators) still
    // reads the UNSCALED max. It also never RAISES health -- the `continue`
    // makes it one-directional.
    //
    // FLOAT EXACTNESS: the Java is a C-style truncation of a float product,
    // `(int)((float)maxHealth * 0.75f)`, not MathUtils.floor and not the integer
    // `maxHealth * 3 / 4`. Written the same way here so the two agree bit for
    // bit at every maxHealth (MODIFIER_AMT = 0.25f, PreservedInsect.java:19;
    // 1.0f - 0.25f is exact in binary, so the constant folds to 0.75f).
    //
    // Applied ONCE, at battle start: monsters spawned later (slime splits) are
    // not scaled, which is the Java's shape -- the loop runs in atBattleStart
    // and nowhere else.
    if (hook != RelicHook::AT_BATTLE_START || !combat_is_elite_room(s.flags)) {
        return;
    }
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        MonsterState& m = s.monsters[i];
        const int32_t capped = static_cast<int32_t>(
            static_cast<float>(m.max_hp) * (1.0f - 0.25f));
        if (m.hp <= capped) {
            continue;
        }
        m.hp = static_cast<int16_t>(capped);
    }
}

// Toy Ornithopter -- heal 5 on potion use. NOT deferred and NOT missing: it is
// LIVE, dispatched on the RunState-owned potion route
// (dispatch_run_relics_on_use_potion, run_advance.cpp) rather than from a
// CombatState-only relic hook, so this combat-side entry is empty by design.
void relic_native_toy_ornithopter(CombatState& /*s*/, RelicHook /*hook*/,
                                  RelicSlot& /*slot*/,
                                  const RelicHookContext& /*ctx*/) noexcept {}

}  // namespace sts::engine
