// SHOP-tier relics -- native hook bodies. Parameters a body does not read are
// left unnamed to keep -Wextra quiet; the signature is the uniform RelicNativeFn.

#include "relics_shop.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/cards.hpp"         // card_def, CardType (STATUS guard)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/run_state.hpp"     // RelicSlot
#include "sts/engine/types.hpp"

namespace sts::engine {

void relic_native_brimstone(CombatState& s, RelicHook hook, RelicSlot& /*slot*/,
                            const RelicHookContext& /*ctx*/) noexcept {
    // Brimstone.atTurnStart (Brimstone.java:44-51):
    //     addToBot(RelicAboveCreatureAction)                 -- cosmetic
    //     addToTop(ApplyPowerAction(player, player, Strength 2))
    //     for (m : monsters) addToTop(ApplyPowerAction(m, m, Strength 1))
    //
    // The calls are reproduced in Java ORDER rather than encoding their result,
    // because successive addToTop pushes REVERSE: after the loop the queue front
    // is the LAST monster, then the earlier monsters in descending slot order,
    // and the player's +2 last of the three groups. Writing the loop the other
    // way round would look tidier and be wrong.
    if (hook != RelicHook::AT_TURN_START) {
        return;
    }
    ActionQueueItem self{};
    self.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    self.src = kActorPlayer;
    self.tgt = kActorPlayer;
    self.amount = 2;
    self.flags = make_apply_power_flags(PowerId::STRENGTH);
    add_to_top(s, self);
    // getMonsters().monsters is every monster in the group, dead ones included,
    // so the loop is over EVERY slot rather than the live ones: the game pushes
    // an ApplyPowerAction for a corpse too, and ApplyPowerAction.update discards
    // it on resolve (ApplyPowerAction.java:96-99). Filtering here instead would
    // change the queue contents, which the observation layer can see.
    for (uint8_t m = 0; m < s.monster_count; ++m) {
        ActionQueueItem mon{};
        mon.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        mon.src = m;
        mon.tgt = m;
        mon.amount = 1;
        mon.flags = make_apply_power_flags(PowerId::STRENGTH);
        add_to_top(s, mon);
    }
}

void relic_native_hand_drill(CombatState& s, RelicHook hook, RelicSlot& /*slot*/,
                             const RelicHookContext& ctx) noexcept {
    // HandDrill.onBlockBroken (HandDrill.java:31-35): addToBot
    // ApplyPowerAction(m, player, VulnerablePower(m, 2, false), 2), where `m` is
    // the creature whose block just broke. Native, not a data step: no
    // StepTarget encodes "the monster from the event".
    if (hook != RelicHook::ON_BLOCK_BROKEN) {
        return;
    }
    ActionQueueItem p{};
    p.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    p.src = kActorPlayer;  // source is the player -> Champion Belt can see it
    p.tgt = ctx.block_broken_monster;
    p.amount = 2;
    p.flags = make_apply_power_flags(PowerId::VULNERABLE);
    add_to_bottom(s, p);
}

void relic_native_medical_kit(CombatState& s, RelicHook hook, RelicSlot& /*slot*/,
                              const RelicHookContext& ctx) noexcept {
    // MedicalKit.onUseCard (MedicalKit.java:1153-1159): a played STATUS card sets
    // card.exhaust = true and action.exhaustCard = true. Both reduce to the same
    // thing here: set the played INSTANCE's EXHAUST flag, which
    // resolve_card_play reads AFTER the hook fan-out when it moves the card out
    // of hand -- the Blue Candle mechanism.
    if (hook != RelicHook::ON_USE_CARD) {
        return;
    }
    const CardDef* cd = card_def(static_cast<CardId>(ctx.card_id));
    if (cd == nullptr || cd->type != CardType::STATUS) {
        return;
    }
    if (ctx.card_pool_index < kCardPoolCap) {
        s.card_pool[ctx.card_pool_index].flags |= card_flag_bit(CardFlag::EXHAUST);
    }
}

// --- DEFERRED combat bodies --------------------------------------------------

void relic_native_orange_pellets(CombatState& s, RelicHook hook,
                                 RelicSlot& /*slot*/,
                                 const RelicHookContext& ctx) noexcept {
    // OrangePellets (OrangePellets.java, 65 lines; the row and ledger row 75
    // previously cited :1218-1250, which does not exist).
    //
    // atTurnStart (:34-39): SKILL = POWER = ATTACK = false.
    // onUseCard (:41-58):
    //     if (card.type == ATTACK) ATTACK = true;
    //     else if (card.type == SKILL) SKILL = true;
    //     else if (card.type == POWER) POWER = true;
    //     if (ATTACK && SKILL && POWER) {
    //         flash();
    //         addToBot(RelicAboveCreatureAction);          -- cosmetic
    //         addToBot(new RemoveDebuffsAction(player));
    //         SKILL = false; POWER = false; ATTACK = false;
    //     }
    //
    // IT CAN FIRE MORE THAN ONCE PER TURN: the latches are cleared ON FIRE, so
    // three more cards of the three types re-arm it within the same turn. The
    // relic's public counter is never touched and stays -1.
    //
    // The latches are CombatState.flags bits, not RelicSlot.counter -- they are
    // `private static` in the Java (combat-global, not per-instance) and the
    // counter is oracle-visible; see kCombatFlagOrangePellets* for the full
    // derivation, including why a value-initialised CombatState is the correct
    // stand-in for an atPreBattle reset the game does not actually do.
    if (hook == RelicHook::AT_TURN_START) {
        s.flags &= ~kCombatFlagOrangePelletsMask;  // (:36-38)
        return;
    }
    if (hook != RelicHook::ON_USE_CARD) {
        return;
    }
    const CardDef* cd = card_def(static_cast<CardId>(ctx.card_id));
    if (cd == nullptr) {
        return;
    }
    // The Java's if/else-if chain: a CURSE or STATUS play sets nothing.
    switch (cd->type) {
        case CardType::ATTACK:
            s.flags |= kCombatFlagOrangePelletsAttack;
            break;
        case CardType::SKILL:
            s.flags |= kCombatFlagOrangePelletsSkill;
            break;
        case CardType::POWER:
            s.flags |= kCombatFlagOrangePelletsPower;
            break;
        default:
            break;
    }
    if ((s.flags & kCombatFlagOrangePelletsMask) !=
        kCombatFlagOrangePelletsMask) {
        return;
    }
    ActionQueueItem clear{};
    clear.opcode = static_cast<uint16_t>(Opcode::REMOVE_DEBUFFS);
    clear.src = kActorPlayer;
    clear.tgt = kActorPlayer;
    add_to_bottom(s, clear);  // addToBot (OrangePellets.java:55)
    s.flags &= ~kCombatFlagOrangePelletsMask;  // (:56-58) -- re-armable
}

void relic_native_sling_of_courage(CombatState& s, RelicHook hook,
                                   RelicSlot& /*slot*/,
                                   const RelicHookContext& /*ctx*/) noexcept {
    // Sling.atBattleStart (Sling.java:30-37 -- the ledger's :1030-1038 was off by
    // exactly 1000; the file is 44 lines):
    //     if (AbstractDungeon.getCurrRoom().eliteTrigger) {
    //         flash();
    //         addToTop(ApplyPowerAction(player, player, StrengthPower(player, 2), 2));
    //         addToTop(RelicAboveCreatureAction(player, this));   -- cosmetic
    //     }
    //
    // eliteTrigger is a BOSS-EXCLUDING test: MonsterRoomBoss never sets the field
    // (MonsterRoomBoss.java:22-24), so this relic does NOT fire on the Act-1 boss.
    // Sling and Slaver's Collar are therefore NOT twins -- the collar ORs an
    // EnemyType.BOSS scan on top (SlaversCollar.java:47-51) and this does not.
    //
    // The two addToTop pushes reverse, leaving the cosmetic
    // RelicAboveCreatureAction in front of the ApplyPowerAction; with the cosmetic
    // dropped, one add_to_top is the whole body. atBattleStart is dispatched after
    // this engine's turn-1 pump (run_advance.cpp step 10), so the Strength lands
    // before control returns to the player -- state-equivalent, because nothing
    // reads Strength between the opening draw and the first card play.
    if (hook != RelicHook::AT_BATTLE_START || !combat_is_elite_room(s.flags)) {
        return;
    }
    ActionQueueItem gain{};
    gain.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    gain.src = kActorPlayer;
    gain.tgt = kActorPlayer;
    gain.amount = 2;
    gain.flags = make_apply_power_flags(PowerId::STRENGTH);
    add_to_top(s, gain);  // addToTop (Sling.java:34)
}

}  // namespace sts::engine
