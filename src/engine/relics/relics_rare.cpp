// RARE-tier relics -- native hook bodies. Parameters a body does not read are
// left unnamed to keep -Wextra quiet; the signature is the uniform RelicNativeFn.
//
// Provenance for each relic is on its registry row; the per-body comments here
// cite the exact Java lines the body mirrors.

#include "relics_rare.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/cards.hpp"         // card_def, CardType (POWER guard) + kIroncladCombatPool
#include "sts/engine/rng_stream.hpp"    // random (Dead Branch's cardRandomRng pick)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags, make_damage_flags
#include "sts/engine/run_state.hpp"     // RelicSlot
#include "sts/engine/types.hpp"

namespace sts::engine {

namespace {

// ApplyPowerAction(player, player, <Power>, n), queued at the TOP of the action
// queue -- the shape every battle-start Strength relic in this tier uses.
void queue_self_power_top(CombatState& s, PowerId power, int32_t amount) noexcept {
    ActionQueueItem p{};
    p.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    p.src = kActorPlayer;
    p.tgt = kActorPlayer;
    p.amount = amount;
    p.flags = make_apply_power_flags(power);
    add_to_top(s, p);
}

}  // namespace

void relic_native_bird_faced_urn(CombatState& s, RelicHook hook,
                                 RelicSlot& /*slot*/,
                                 const RelicHookContext& ctx) noexcept {
    // BirdFacedUrn.onUseCard (BirdFacedUrn.java:33-40): a played POWER card
    // addToTop HealAction(player, player, 2). No opcode encodes a heal, and a
    // pure heal has no queue-ordering interplay with the other S1 relic effects,
    // so it is applied directly at dispatch time -- through the shared seam, so
    // Magic Flower's x1.5 applies (MagicFlower.java:30-37).
    if (hook != RelicHook::ON_USE_CARD) {
        return;
    }
    const CardDef* cd = card_def(static_cast<CardId>(ctx.card_id));
    if (cd == nullptr || cd->type != CardType::POWER) {
        return;
    }
    heal_player_with_relics(s, 2);
}

void relic_native_captains_wheel(CombatState& s, RelicHook hook, RelicSlot& slot,
                                 const RelicHookContext& /*ctx*/) noexcept {
    // CaptainsWheel (CaptainsWheel.java:31-53): atBattleStart arms (counter =
    // 0); atTurnStart increments WHILE NOT grayscale and fires once at turn 3
    // (18 block), then counter = -1 and grayscale = true; onVictory counter = -1.
    // counter < 0 IS the grayscale latch: the Java sets both together and never
    // increments a grayscale relic, so the sign carries the whole state. Exactly
    // Horn Cleat's encoding, with 3/18 instead of 2/14.
    if (hook == RelicHook::AT_BATTLE_START) {
        slot.counter = 0;
    } else if (hook == RelicHook::AT_TURN_START) {
        if (slot.counter < 0) {
            return;  // grayscale: no further increments
        }
        ++slot.counter;
        if (slot.counter == 3) {
            ActionQueueItem blk{};
            blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
            blk.src = kActorPlayer;
            blk.tgt = kActorPlayer;
            blk.amount = 18;
            blk.flags = kBlockNoPowers;  // direct GainBlockAction: no Dexterity
            add_to_bottom(s, blk);       // addToBot (CaptainsWheel.java:43)
            slot.counter = -1;
        }
    } else if (hook == RelicHook::ON_VICTORY) {
        slot.counter = -1;  // CaptainsWheel.java:50-53
    }
}

void relic_native_charons_ashes(CombatState& s, RelicHook hook,
                                RelicSlot& /*slot*/,
                                const RelicHookContext& /*ctx*/) noexcept {
    // CharonsAshes.onExhaust (CharonsAshes.java:36-43): addToTop
    // DamageAllEnemiesAction(createDamageMatrix(3, true), DamageType.THORNS).
    // Native rather than a data step purely for the queue END -- a data-bound
    // relic step is always addToBot, and on_exhaust fires mid-resolution.
    if (hook != RelicHook::ON_EXHAUST) {
        return;
    }
    ActionQueueItem d{};
    d.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    d.src = kActorPlayer;
    d.tgt = kActorAllEnemies;
    d.amount = 3;
    d.flags = make_damage_flags(DamageType::THORNS);
    add_to_top(s, d);
}

void relic_native_du_vu_doll(CombatState& s, RelicHook hook, RelicSlot& slot,
                             const RelicHookContext& /*ctx*/) noexcept {
    // DuVuDoll.atBattleStart (DuVuDoll.java:69-75): if counter > 0, addToTop
    // ApplyPowerAction(player, player, StrengthPower(counter), counter). The
    // counter is the master deck's CURSE count, maintained by onEquip /
    // onMasterDeckChange at the run layer (relic_pickup_rare.cpp, run_deck.hpp).
    if (hook == RelicHook::AT_BATTLE_START && slot.counter > 0) {
        queue_self_power_top(s, PowerId::STRENGTH, slot.counter);
    }
}

void relic_native_girya(CombatState& s, RelicHook hook, RelicSlot& slot,
                        const RelicHookContext& /*ctx*/) noexcept {
    // Girya.atBattleStart (Girya.java:38-44): if counter != 0, addToTop
    // ApplyPowerAction(player, player, StrengthPower(counter), counter). NOT
    // `> 0` -- the Java tests inequality, and the counter only ever rises via the
    // campfire Lift option, which is not implemented, so the branch is a live
    // no-op at counter 0 rather than an unreachable one.
    if (hook == RelicHook::AT_BATTLE_START && slot.counter != 0) {
        queue_self_power_top(s, PowerId::STRENGTH, slot.counter);
    }
}

void relic_native_incense_burner(CombatState& s, RelicHook hook, RelicSlot& slot,
                                 const RelicHookContext& /*ctx*/) noexcept {
    // IncenseBurner.atTurnStart (IncenseBurner.java:36-44):
    //   counter = (counter == -1) ? counter + 2 : counter + 1;
    //   if (counter == 6) { counter = 0; addToBot ApplyPowerAction(player, null,
    //                                     IntangiblePlayerPower 1); }
    // The -1 case is the unset/save-loaded counter and is reproduced literally
    // (it lands on 1, not 0, so a restored relic does not lose a turn).
    if (hook != RelicHook::AT_TURN_START) {
        return;
    }
    slot.counter = static_cast<int16_t>(slot.counter == -1 ? slot.counter + 2
                                                           : slot.counter + 1);
    if (slot.counter == 6) {
        slot.counter = 0;
        ActionQueueItem p{};
        p.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        p.src = kActorPlayer;
        p.tgt = kActorPlayer;
        p.amount = 1;
        p.flags = make_apply_power_flags(PowerId::INTANGIBLE);
        add_to_bottom(s, p);  // addToBot (IncenseBurner.java:42)
    }
}

void relic_native_pocketwatch(CombatState& s, RelicHook hook, RelicSlot& slot,
                              const RelicHookContext& /*ctx*/) noexcept {
    // Pocketwatch (Pocketwatch.java:32-60). The Java carries TWO pieces of
    // state: the public `counter` (cards played this turn) and a PRIVATE
    // `firstTurn` boolean the oracle never serializes. They are packed into the
    // one signed counter this engine stores per relic slot:
    //
    //   counter >= 0  -> firstTurn == false, counter == cards played this turn
    //   counter <  0  -> firstTurn == true,  (-counter - 1) == cards played
    //
    // so the Java-visible play count is recoverable at every point, and the
    // opening turn's "do not draw, just disarm" branch is the sign test.
    //
    //   atBattleStart      counter = 0, firstTurn = true
    //   onPlayCard         ++counter
    //   atTurnStartPostDraw  if (counter <= 3 && !firstTurn) addToBot
    //                        DrawCardAction(player, 3) else firstTurn = false;
    //                        counter = 0 (unconditionally, both branches)
    //   onVictory          counter = -1
    const auto plays = [](int16_t c) noexcept -> int {
        return c >= 0 ? c : -c - 1;
    };
    switch (hook) {
        case RelicHook::AT_BATTLE_START:
            slot.counter = -1;  // 0 plays, firstTurn armed
            break;
        case RelicHook::ON_PLAY_CARD:
            // ++counter in the Java; here that is +1 to the magnitude, sign kept.
            slot.counter = slot.counter >= 0
                               ? static_cast<int16_t>(slot.counter + 1)
                               : static_cast<int16_t>(slot.counter - 1);
            break;
        case RelicHook::AT_TURN_START_POST_DRAW: {
            const bool first_turn = slot.counter < 0;
            if (!first_turn && plays(slot.counter) <= 3) {
                ActionQueueItem d{};
                d.opcode = static_cast<uint16_t>(Opcode::DRAW);
                d.src = kActorPlayer;
                d.tgt = kActorPlayer;
                d.amount = 3;
                add_to_bottom(s, d);  // addToBot (Pocketwatch.java:40)
            }
            // Both branches then set counter = 0; the else-branch additionally
            // clears firstTurn, which is the same thing in this encoding.
            slot.counter = 0;
            break;
        }
        case RelicHook::ON_VICTORY:
            slot.counter = -1;  // Pocketwatch.java:57-60
            break;
        default:
            break;
    }
}

void relic_native_stone_calendar(CombatState& s, RelicHook hook, RelicSlot& slot,
                                 const RelicHookContext& /*ctx*/) noexcept {
    // StoneCalendar (StoneCalendar.java:36-68): atBattleStart counter = 0;
    // atTurnStart ++counter; onPlayerEndTurn -- if counter == 7 addToBot
    // DamageAllEnemiesAction(createDamageMatrix(52, true), THORNS); onVictory
    // counter = -1. The equality (not >=) plus the unconditional increment is
    // what makes it fire exactly once: turn 8's ++ carries the counter past 7.
    switch (hook) {
        case RelicHook::AT_BATTLE_START:
            slot.counter = 0;
            break;
        case RelicHook::AT_TURN_START:
            ++slot.counter;
            break;
        case RelicHook::ON_PLAYER_END_TURN:
            if (slot.counter == 7) {
                ActionQueueItem d{};
                d.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
                d.src = kActorPlayer;
                d.tgt = kActorAllEnemies;
                d.amount = 52;
                d.flags = make_damage_flags(DamageType::THORNS);
                add_to_bottom(s, d);  // addToBot (StoneCalendar.java:53)
            }
            break;
        case RelicHook::ON_VICTORY:
            slot.counter = -1;  // StoneCalendar.java:65-68
            break;
        default:
            break;
    }
}

// --- DEFERRED combat bodies --------------------------------------------------
//
// Under the generated dispatch a `native: true` row with no definition is a link
// error, so "registered but not implemented yet" cannot be expressed by leaving
// the function out. It is an explicit empty body carrying the reason and the
// citation, right where the implementation goes.

void relic_native_dead_branch(CombatState& s, RelicHook hook,
                              RelicSlot& /*slot*/,
                              const RelicHookContext& /*ctx*/) noexcept {
    // DeadBranch.onExhaust (DeadBranch.java:24-31 -- the file is 43 lines; the
    // row and ledger row 72 cited :259-266):
    //     if (!getMonsters().areMonstersBasicallyDead()) {
    //         flash();
    //         addToBot(RelicAboveCreatureAction);                  -- cosmetic
    //         addToBot(new MakeTempCardInHandAction(
    //             AbstractDungeon.returnTrulyRandomCardInCombat().makeCopy(), false));
    //     }
    //
    // THE DRAW HAPPENS AT QUEUE TIME, and that is the point rather than an
    // implementation detail: returnTrulyRandomCardInCombat() is an ARGUMENT of
    // the MakeTempCardInHandAction constructor, so the cardRandomRng pick is
    // spent WHILE THIS HOOK RUNS -- inside the exhaust fan-out -- and only the
    // card creation is deferred to the queue. Rolling it at resolve time instead
    // would move the stream the moment anything else queued between the two
    // consumes cardRandomRng. Magnetism's atStartOfTurn is the same shape and the
    // same argument (power_magnetism.cpp).
    //
    // THE POOL ALREADY EXISTED. returnTrulyRandomCardInCombat()
    // (AbstractDungeon.java:938-962) concatenates srcCommon + srcUncommon +
    // srcRare EXCLUDING CardTags.HEALING and takes
    // `list.get(cardRandomRng.random(list.size() - 1))` -- one draw. That is
    // exactly kIroncladCombatPool (70 rows, generated), the pool Discovery
    // already consumes. The deferral note claimed the draw was UNFILTERED and so
    // needed a second generated pool; it is HEALING-filtered, and it does not.
    // (The genuinely-new pool is Pandora's Box's returnTrulyRandomCard(), which
    // does NOT exclude HEALING -- 72 rows, still unbuilt.)
    //
    // areMonstersBasicallyDead (MonsterGroup.java:90-95) is `isDying ||
    // isEscaping` per monster, which is monster_dead_or_escaped here -- the same
    // gate Dark Embrace and Magnetism use, spelled inline for the same reason.
    if (hook != RelicHook::ON_EXHAUST) {
        return;
    }
    bool any_live = false;
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        any_live = any_live || !monster_dead_or_escaped(s.monsters[i]);
    }
    if (!any_live) {
        return;
    }
    const int32_t pick =
        random(s.card_random_rng,
               static_cast<int32_t>(kIroncladCombatPoolCount) - 1);
    const CardId id = kIroncladCombatPool[static_cast<unsigned>(pick)];
    ActionQueueItem make{};
    make.opcode = static_cast<uint16_t>(Opcode::MAKE_CARD);
    make.src = static_cast<uint8_t>(CardPile::HAND);
    make.tgt = kActorPlayer;
    make.amount = 1;
    make.flags = make_make_card_flags(static_cast<uint16_t>(id));
    add_to_bottom(s, make);  // addToBot (DeadBranch.java:29)
}

void relic_native_gambling_chip(CombatState& s, RelicHook hook,
                                RelicSlot& slot,
                                const RelicHookContext& /*ctx*/) noexcept {
    // GamblingChip.java, read in full -- the file is 48 lines, not the 426-453
    // range the deferral note cited:
    //
    //   private boolean activated = false;                             (:18)
    //   public void atBattleStartPreDraw() { this.activated = false; } (:30-32)
    //   public void atTurnStartPostDraw() {                            (:34-42)
    //       if (!this.activated) {
    //           this.activated = true;
    //           this.flash();
    //           this.addToBot(new RelicAboveCreatureAction(player, this));
    //           this.addToBot(new GamblingChipAction(AbstractDungeon.player));
    //       }
    //   }
    //
    // ONCE PER COMBAT, NOT EVERY TURN. atTurnStartPostDraw fires every turn but
    // the `activated` latch lets the body run only on the first. `slot.counter`
    // carries the latch: the registry row leaves it at the -1 unset default,
    // which IS "not yet activated", and a fresh CombatState starts there -- so
    // no combat-scoped reset is needed at this hook, and binding
    // atBattleStartPreDraw (the Java's reset) would be inert code, since the
    // engine has no dispatch site for it.
    //
    // The queued action is the SAME GamblingChipAction Gambler's Brew queues,
    // one presentation-only boolean apart (notChip false here, true there,
    // selecting only the prompt string, GamblingChipAction.java:42-46), so this
    // calls the shared builder rather than hand-writing a second item. The draw
    // count is `selectedCards.size()` read AT CONFIRM -- there is no
    // relic-side count.
    //
    // RelicAboveCreatureAction (:39) is pure presentation and is not modeled.
    //
    // This body was deferred on the grounds that "every existing ChoiceKind
    // selects a MANDATORY fixed count, so the engine has no optional
    // multi-select with an explicit confirm". That prerequisite ARRIVED --
    // kChoiceOptionalBit, ActionVerb::CONFIRM and the runtime selected-count
    // nibble are live -- which is exactly the conventions §8 signal that a
    // comment justifying inert code by a missing prerequisite has gone stale.
    if (hook != RelicHook::AT_TURN_START_POST_DRAW || slot.counter != -1) {
        return;
    }
    slot.counter = 1;               // this.activated = true (:36)
    queue_gambling_chip_choice(s);  // addToBot (:40)
}

}  // namespace sts::engine
