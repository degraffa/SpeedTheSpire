// SPECIAL-tier relics -- native hook bodies. Parameters a body does not read are
// left unnamed to keep -Wextra quiet; the signature is the uniform RelicNativeFn.
//
// Provenance for each relic is on its registry row; the per-body comments here
// cite the exact Java lines the body mirrors.

#include "relics_special.hpp"

#include <cstdint>

#include "sts/engine/combat_state.hpp"
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

// --- DEFERRED combat bodies --------------------------------------------------

void relic_native_warped_tongs(CombatState& /*s*/, RelicHook /*hook*/,
                               RelicSlot& /*slot*/,
                               const RelicHookContext& /*ctx*/) noexcept {
    // WarpedTongs.atTurnStartPostDraw (WarpedTongs.java:24-29): addToBot
    // RelicAboveCreatureAction, then addToBot UpgradeRandomCardAction.
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
    // would silently move shuffleRng. Implementing this needs its own opcode.
    // The registry row still BINDS at_turn_start_post_draw so the queue position
    // (behind the start-of-turn DrawCardAction, GameActionManager.java:361-362) is
    // already correct the day the opcode lands.
}

}  // namespace sts::engine
