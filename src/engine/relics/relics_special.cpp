// SPECIAL-tier relics -- native hook bodies. Parameters a body does not read are
// left unnamed to keep -Wextra quiet; the signature is the uniform RelicNativeFn.
//
// Provenance for each relic is on its registry row; the per-body comments here
// cite the exact Java lines the body mirrors.

#include "relics_special.hpp"

#include <cstdint>

#include "../interp/interp_cards.hpp"   // op_play_card (Necronomicon's replay)
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/cards.hpp"         // card_def, CardType (Necronomicon)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, kPlayCard*, kRandomToHandPool*
#include "sts/engine/run_state.hpp"  // RelicSlot
#include "sts/engine/types.hpp"

namespace sts::engine {

void relic_native_neows_lament(CombatState& s, RelicHook hook, RelicSlot& slot,
                               const RelicHookContext& /*ctx*/) noexcept {
    // NeowsLament.atBattleStart (NeowsLament.java:26-45):
    //
    //   if (this.counter > 0) {
    //       --this.counter;
    //       if (this.counter == 0) { setCounter(-2); ...description... }
    //       this.flash();
    //       for (m : getCurrRoom().monsters.monsters) { m.currentHealth = 1; ... }
    //       addToTop(RelicAboveCreatureAction(player, this));
    //   }
    //
    // Two details are load-bearing:
    //
    // (1) The DECREMENT COMES FIRST and the HP write is unconditional inside the
    //     branch, so the THIRD combat (counter 1 -> 0) still gets the effect and
    //     the relic is only spent afterwards. setCounter(-2) is AbstractRelic's
    //     used-up marker (NeowsLament.java:47-53), which is why the counter goes
    //     to -2 rather than staying at 0.
    // (2) `m.currentHealth = 1` is a RAW FIELD WRITE. It is not damage: no block,
    //     no damage pipeline, no death check, no onMonsterDeath. A monster already
    //     at 1 HP or below is likewise SET to 1 -- the Java has no clamp and no
    //     liveness filter, and it walks the whole monsters list, so this loop
    //     mirrors that rather than filtering to the live slots.
    if (hook != RelicHook::AT_BATTLE_START || slot.counter <= 0) {
        return;
    }
    --slot.counter;
    if (slot.counter == 0) {
        slot.counter = -2;  // setCounter(-2) -> usedUp
    }
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        if (s.monsters[i].monster_id == static_cast<uint16_t>(MonsterId::NONE)) {
            continue;
        }
        s.monsters[i].hp = 1;
    }
}

void relic_native_face_of_cleric(CombatState& s, RelicHook hook,
                                 RelicSlot& /*slot*/,
                                 const RelicHookContext& /*ctx*/) noexcept {
    // FaceOfCleric.onVictory (FaceOfCleric.java:22-26): increaseMaxHp(1, true).
    // AbstractPlayer.increaseMaxHp(amount, heal) raises maxHealth by the amount
    // and, with `heal` set, heals the same amount -- so BOTH current and max HP
    // rise by 1 (the current-HP rise is a heal, hence clamped to the NEW max,
    // which the +1 has already made room for).
    //
    // Deliberately NOT routed through heal_player_with_relics: increaseMaxHp's
    // heal is AbstractPlayer.heal, so Magic Flower would in principle see it --
    // but the amount is 1 and MathUtils.round(1 * 1.5f) is 2, which would push
    // current HP one point ABOVE the new max and get clamped straight back to it.
    // Writing the clamped result directly is the same number by the shorter road,
    // and the comment is here so the shortcut is visible rather than assumed.
    if (hook != RelicHook::ON_VICTORY) {
        return;
    }
    s.player_max_hp = static_cast<int16_t>(s.player_max_hp + 1);
    if (s.player_hp < s.player_max_hp) {
        s.player_hp = static_cast<int16_t>(s.player_hp + 1);
    }
}

void relic_native_gremlin_mask(CombatState& s, RelicHook hook,
                               RelicSlot& /*slot*/,
                               const RelicHookContext& /*ctx*/) noexcept {
    if (hook != RelicHook::AT_BATTLE_START) {
        return;
    }
    ActionQueueItem weak{};
    weak.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    weak.src = kActorPlayer;
    weak.tgt = kActorPlayer;
    weak.amount = 1;
    // WeakPower(player, 1, false): the false prevents the justApplied latch, so
    // this one-turn Weak expires at the first round end.
    weak.flags = make_apply_power_flags(PowerId::WEAK, 0, false);
    add_to_bottom(s, weak);
}

void relic_native_red_mask(CombatState& s, RelicHook hook,
                           RelicSlot& /*slot*/,
                           const RelicHookContext& /*ctx*/) noexcept {
    if (hook != RelicHook::AT_BATTLE_START) {
        return;
    }
    // RedMask.atBattleStart (RedMask.java:33-40): per group member, a cosmetic
    // RelicAboveCreatureAction (queues nothing here) then ApplyPowerAction(mo,
    // player, new WeakPower(mo, 1, false), 1, true) -- Gremlin Mask's shape
    // fanned over the monsters, and the same explicit isSourceMonster=false:
    // a MONSTER-owned Weak would normally take the justApplied end-of-turn
    // latch, but this stack must tick down at the monsters' own first round
    // end. The Java loop iterates every member unconditionally; the shared
    // APPLY_POWER body's isDeadOrEscaped refusal (op_apply_power) is
    // ApplyPowerAction.java:114-118's own drop, so no filter belongs here.
    for (uint8_t mi = 0; mi < s.monster_count; ++mi) {
        ActionQueueItem weak{};
        weak.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        weak.src = kActorPlayer;
        weak.tgt = mi;
        weak.amount = 1;
        weak.flags = make_apply_power_flags(PowerId::WEAK, 0, false);
        add_to_bottom(s, weak);
    }
}

// --- S2.34: the payout-relic combat bodies -----------------------------------

void relic_native_enchiridion(CombatState& s, RelicHook hook,
                              RelicSlot& /*slot*/,
                              const RelicHookContext& /*ctx*/) noexcept {
    // Enchiridion.atPreBattle (Enchiridion.java:30-39): flash, ONE
    // returnTrulyRandomCardInCombat(CardType.POWER) draw, makeCopy() a BASE
    // instance, setCostForTurn(0) unless cost == -1, addToBot
    // MakeTempCardInHandAction(c). That is Infernal Blade's body with one
    // CardType changed, so the queued item is RANDOM_ATTACK_TO_HAND with the
    // S2.34 POWER pool selector -- the draw happens at the item's execute,
    // which is stream-identical here: nothing queued at pre-battle by any
    // in-scope relic consumes cardRandomRng (Snecko Eye's Confusion apply
    // draws only at later card draws), so the fan-out-time draw and the
    // execute-time draw see the same stream position. AT_PRE_BATTLE, not
    // AT_BATTLE_START: the item resolves before the opening draw
    // (combat_begin / enter_combat dispatch this hook ahead of
    // begin_first_turn), so the free power is in hand when turn 1 opens.
    if (hook != RelicHook::AT_PRE_BATTLE) {
        return;
    }
    ActionQueueItem item{};
    item.opcode = static_cast<uint16_t>(Opcode::RANDOM_ATTACK_TO_HAND);
    item.src = kActorPlayer;
    item.tgt = kActorPlayer;
    item.flags = kRandomToHandPoolPower;
    add_to_bottom(s, item);  // addToBot (Enchiridion.java:38)
}

void relic_native_nilrys_codex(CombatState& s, RelicHook hook,
                               RelicSlot& /*slot*/,
                               const RelicHookContext& /*ctx*/) noexcept {
    // NilrysCodex.onPlayerEndTurn (NilrysCodex.java:28-32): addToBot a
    // cosmetic RelicAboveCreatureAction (queues nothing here), then addToBot a
    // new CodexAction(). The whole mechanic lives in the CODEX item: prepared
    // and intercepted at the pump head (the DISCOVERY shape), always
    // skippable, resolving to a random-spot draw-pile insert -- interp.hpp
    // Opcode::CODEX carries the full CodexAction.java derivation.
    if (hook != RelicHook::ON_PLAYER_END_TURN) {
        return;
    }
    ActionQueueItem item{};
    item.opcode = static_cast<uint16_t>(Opcode::CODEX);
    item.src = kActorPlayer;
    item.tgt = kActorPlayer;
    add_to_bottom(s, item);  // addToBot (NilrysCodex.java:31)
}

void relic_native_necronomicon(CombatState& s, RelicHook hook,
                               RelicSlot& /*slot*/,
                               const RelicHookContext& ctx) noexcept {
    // Necronomicon.atTurnStart (Necronomicon.java:82-85): `activated = true`.
    // The latch is the INVERTED kCombatFlagNecronomiconUsed bit
    // (combat_state.hpp), so re-arming CLEARS it; this hook fires on every
    // turn, turn 1 included, which is what makes cross-combat residue
    // unobservable.
    if (hook == RelicHook::AT_TURN_START) {
        s.flags &= ~kCombatFlagNecronomiconUsed;
        return;
    }
    if (hook != RelicHook::ON_USE_CARD) {
        return;
    }
    // Necronomicon.onUseCard (Necronomicon.java:60-80), fired in the
    // UseCardAction-CONSTRUCTOR fan-out AFTER player powers (so after Double
    // Tap) and BEFORE monster powers. The gate (:62):
    //     card.type == ATTACK
    //  && (card.costForTurn >= 2 && !card.freeToPlayOnce
    //      || card.cost == -1 && card.energyOnUse >= 2)
    //  && this.activated
    // Note there is NO !purgeOnUse conjunct (that is Double Tap's, :44): a
    // replay copy that still meets the cost bar re-triggers, held off only by
    // the once-per-turn latch. The replay itself is Double Tap's exact
    // machinery -- makeSameInstanceOf, applyPowers, purgeOnUse,
    // addCardQueueItem(new CardQueueItem(tmp, m, card.energyOnUse, true,
    // true), true) == front-of-queue autoplay (:70-77) -- so it calls the
    // shared PLAY_CARD verb synchronously, exactly as power_double_tap.cpp
    // does and for the same reason (the enqueue happens inside the
    // constructor, not through a queued action).
    if ((s.flags & kCombatFlagNecronomiconUsed) != 0u ||
        ctx.card_pool_index >= kCardPoolCap) {
        return;
    }
    const CardDef* cd = card_def(static_cast<CardId>(ctx.card_id));
    if (cd == nullptr || cd->type != CardType::ATTACK) {
        return;  // `card.type == CardType.ATTACK` (:62)
    }
    const CardInstance& card = s.card_pool[ctx.card_pool_index];
    const bool is_xcost = has_card_flag(card.flags, CardFlag::XCOST);
    const bool cost_arm =
        !is_xcost && static_cast<int>(card.cost_now) >= 2 &&
        !has_card_flag(card.flags, CardFlag::FREE_TO_PLAY_ONCE);
    const bool xcost_arm = is_xcost && ctx.energy_on_use >= 2;
    if (!cost_arm && !xcost_arm) {
        return;
    }
    s.flags |= kCombatFlagNecronomiconUsed;  // this.activated = false (:63)
    op_play_card(s, ctx.target, static_cast<int>(ctx.card_pool_index),
                 kPlayCardCopy | kPlayCardPurge | kPlayCardQueueFront);
}

void relic_native_mutagenic_strength(CombatState& s, RelicHook hook,
                                     RelicSlot& /*slot*/,
                                     const RelicHookContext& /*ctx*/) noexcept {
    // MutagenicStrength.atBattleStart (MutagenicStrength.java:31-37): THREE
    // addToTop calls in source order -- ApplyPowerAction(StrengthPower 3),
    // ApplyPowerAction(LoseStrengthPower 3), cosmetic RelicAboveCreatureAction
    // -- so the RESOLUTION order is the reverse: cosmetic (queues nothing),
    // LoseStrength 3, THEN Strength 3, and the power list ends up
    // [LOSE_STRENGTH, STRENGTH], which is publicly observable slot order.
    // NATIVE for exactly that reason (the registry row says why): a data
    // program queues addToBot in list order and cannot reproduce the addToTop
    // reversal at this queue position. add_to_top'ing Strength FIRST then
    // LoseStrength lands them in the Java's resolution order.
    if (hook != RelicHook::AT_BATTLE_START) {
        return;
    }
    ActionQueueItem str{};
    str.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    str.src = kActorPlayer;
    str.tgt = kActorPlayer;
    str.amount = 3;
    str.flags = make_apply_power_flags(PowerId::STRENGTH);
    add_to_top(s, str);
    ActionQueueItem lose{};
    lose.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    lose.src = kActorPlayer;
    lose.tgt = kActorPlayer;
    lose.amount = 3;
    lose.flags = make_apply_power_flags(PowerId::LOSE_STRENGTH);
    add_to_top(s, lose);
}

void relic_native_warped_tongs(CombatState& s, RelicHook hook,
                               RelicSlot& /*slot*/,
                               const RelicHookContext& /*ctx*/) noexcept {
    // WarpedTongs.atTurnStartPostDraw (WarpedTongs.java:28-33 -- the row and
    // ledger cited :24-29; the file is 40 lines): flash(), addToBot a cosmetic
    // RelicAboveCreatureAction, addToBot a new UpgradeRandomCardAction().
    //
    // RNG-VISIBLE (and LIVE -- this comment's old "DEFERRED" went stale when
    // Opcode::UPGRADE_RANDOM_CARD landed; corrected by S2.34, the conventions
    // §8 "comment asserting X" shape). UpgradeRandomCardAction.update
    // (UpgradeRandomCardAction.java:28-50):
    //     if (hand.isEmpty()) { done; }                       -- no draw
    //     upgradeable = hand filtered to canUpgrade() && type != STATUS
    //     if (upgradeable.size() > 0) {                       -- else no draw
    //         upgradeable.shuffle();                          -- CardGroup.shuffle()
    //         upgradeable.group.get(0).upgrade();
    //     }
    // The no-argument CardGroup.shuffle() seeds its java.util.Random from
    // shuffleRng.randomLong() (CardGroup.java:561-563) -- one draw, and ONLY when
    // the filtered subset is non-empty.
    //
    // CHOOSE_CARD's RANDOM + UPGRADE path is NOT this action: it draws a
    // different stream and applies a different eligibility filter, so reusing it
    // would silently move shuffleRng. Hence Opcode::UPGRADE_RANDOM_CARD, whose
    // body (op_upgrade_random_card, interp/interp_cards.cpp) carries the whole
    // derivation including the zero-draw paths.
    //
    // The queue position is the row's, not this body's: at_turn_start_post_draw
    // fires behind the start-of-turn DrawCardAction
    // (GameActionManager.java:361-362 on turn >= 2, AbstractRoom.java:242 on turn
    // 1), which is what makes "upgrade a card in hand" see the hand it should.
    //
    // The upgrade is per-COMBAT: it writes the card_pool INSTANCE, which is built
    // from the master deck at combat construction and discarded at combat end, so
    // nothing folds back to RunState. That matches the Java, where the action
    // upgrades the in-combat AbstractCard copy.
    if (hook != RelicHook::AT_TURN_START_POST_DRAW) {
        return;
    }
    ActionQueueItem up{};
    up.opcode = static_cast<uint16_t>(Opcode::UPGRADE_RANDOM_CARD);
    up.src = kActorPlayer;
    up.tgt = kActorPlayer;
    add_to_bottom(s, up);  // addToBot (WarpedTongs.java:32)
}

}  // namespace sts::engine
