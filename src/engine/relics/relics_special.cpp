// SPECIAL-tier relics -- native hook bodies. Parameters a body does not read are
// left unnamed to keep -Wextra quiet; the signature is the uniform RelicNativeFn.
//
// Provenance for each relic is on its registry row; the per-body comments here
// cite the exact Java lines the body mirrors.

#include "relics_special.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode
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

// --- DEFERRED combat bodies --------------------------------------------------

void relic_native_warped_tongs(CombatState& s, RelicHook hook,
                               RelicSlot& /*slot*/,
                               const RelicHookContext& /*ctx*/) noexcept {
    // WarpedTongs.atTurnStartPostDraw (WarpedTongs.java:28-33 -- the row and
    // ledger cited :24-29; the file is 40 lines): flash(), addToBot a cosmetic
    // RelicAboveCreatureAction, addToBot a new UpgradeRandomCardAction().
    //
    // DEFERRED, and RNG-VISIBLE when it lands. UpgradeRandomCardAction.update
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
