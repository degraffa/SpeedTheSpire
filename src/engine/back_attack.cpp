// Back Attack -- AbstractMonster's facing machinery. See back_attack.hpp for the
// four readings this file implements (the predicate, the resolved facing
// question, the two 1.5x sites and their differing arithmetic, and the marker's
// one declared deviation).

#include "sts/engine/back_attack.hpp"

#include "sts/engine/action_queue.hpp"  // add_to_top, ActionQueueItem, kActorPlayer
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"  // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

[[nodiscard]] bool actor_holds(const CombatState& s, uint8_t actor,
                               PowerId pid) noexcept {
    const PowerSlot* slots = nullptr;
    uint8_t count = 0;
    if (actor == kActorPlayer) {
        slots = s.player_powers;
        count = s.player_power_count;
    } else if (actor < kMonsterCap) {
        slots = s.monsters[actor].powers;
        count = s.monsters[actor].power_count;
    } else {
        return false;
    }
    for (uint8_t i = 0; i < count; ++i) {
        if (slots[i].power_id == static_cast<uint16_t>(pid)) {
            return true;
        }
    }
    return false;
}

// MonsterGroup.areMonstersBasicallyDead (:90-95) over the whole group -- the
// early return refreshHandLayout opens with (:201-203).
[[nodiscard]] bool group_is_basically_dead(const CombatState& s) noexcept {
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        if (!monster_basically_dead(s.monsters[i])) {
            return false;
        }
    }
    return true;
}

}  // namespace

// The POSITIONAL half of applyBackAttack -- CardGroup.refreshHandLayout
// (:206-221) tests exactly this half, having already established
// `player.hasPower("Surrounded")` once at :204.
bool monster_back_attacked_by_position(const CombatState& s,
                                       uint8_t mi) noexcept {
    if (mi >= kMonsterCap || mi >= s.monster_count) {
        return false;
    }
    const int16_t mx = s.monsters[mi].draw_x;
    return player_facing_left(s.flags) ? (kPlayerDrawXInGuardRoom < mx)
                                       : (kPlayerDrawXInGuardRoom > mx);
}

bool monster_applies_back_attack(const CombatState& s, uint8_t mi) noexcept {
    if (mi >= kMonsterCap || mi >= s.monster_count) {
        return false;
    }
    // The hasPower conjunct is FIRST and short-circuits: in every combat that is
    // not the Act-4 elite the player never holds Surrounded, so this costs one
    // scan of a list that is usually empty and nothing else.
    if (!actor_holds(s, kActorPlayer, PowerId::SURROUNDED)) {
        return false;
    }
    return monster_back_attacked_by_position(s, mi);
}

int back_attack_multiply(int output) noexcept {
    // `dmg.output = (int)((float)dmg.output * 1.5f)` (AbstractMonster.java:1006).
    // Written with the same widening/narrowing the Java performs -- int -> float,
    // multiply by the float literal 1.5f, C-cast back to int (truncation toward
    // zero) -- rather than as `output * 3 / 2`, because the two agree only while
    // the product is exactly representable. 1.5f is a power-of-two-scaled dyadic
    // and every reachable `output` is far inside float's exact-integer range, so
    // they DO agree today; the float spelling is kept anyway because it is what
    // the game executes and this file is the citation.
    return static_cast<int>(static_cast<float>(output) * 1.5f);
}

void set_player_facing_toward(CombatState& s, uint8_t mi) noexcept {
    if (mi >= kMonsterCap) {
        return;
    }
    // `player.flipHorizontal = m.drawX < player.drawX` -- SET (facing left) when
    // the chosen monster sits to the player's LEFT.
    if (s.monsters[mi].draw_x < kPlayerDrawXInGuardRoom) {
        s.flags |= kCombatFlagPlayerFacingLeft;
    } else {
        s.flags &= ~kCombatFlagPlayerFacingLeft;
    }
}

void refresh_back_attack_markers(CombatState& s) noexcept {
    // `if (monsters.areMonstersBasicallyDead()) return;` (CardGroup.java:201-203)
    // -- ahead of the Surrounded test, so a group that is already down queues
    // nothing at all.
    if (group_is_basically_dead(s)) {
        return;
    }
    if (!actor_holds(s, kActorPlayer, PowerId::SURROUNDED)) {
        return;  // `if (player.hasPower("Surrounded") ...)` (:204)
    }
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        // NOT liveness-filtered: the Java walks `monsters.monsters` whole, and
        // both queued forms carry their own resolve-time guards (op_apply_power's
        // isDeadOrEscaped early-out; op_remove_power's find-or-nothing).
        const bool wants = monster_back_attacked_by_position(s, i);
        const bool has = actor_holds(s, i, PowerId::BACK_ATTACK);
        if (wants && !has) {
            // AbstractMonster.applyPowers (:999-1002):
            //   addToTop(new ApplyPowerAction(this, null, new BackAttackPower(this)))
            // The 3-arg ctor forwards the power's own amount (ApplyPowerAction
            // .java:79-81), and BackAttackPower's ctor sets `amount = -1`
            // (BackAttackPower.java:27) -- the game's spelling of "no number",
            // the SurroundedPower / MinionPower shape, carried faithfully because
            // it is oracle-visible.
            ActionQueueItem apply{};
            apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
            apply.src = i;  // ApplyPowerAction(this, null, ...) -- target is self
            apply.tgt = i;
            apply.amount = kBackAttackAppliedAmount;
            apply.flags = make_apply_power_flags(PowerId::BACK_ATTACK);
            add_to_top(s, apply);
        } else if (!wants && has) {
            // AbstractMonster.removeSurroundedPower (:1019-1023):
            //   if (hasPower("BackAttack"))
            //       addToTop(new RemoveSpecificPowerAction(this, null, "BackAttack"))
            ActionQueueItem rem{};
            rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
            rem.src = i;
            rem.tgt = i;
            rem.flags = make_apply_power_flags(PowerId::BACK_ATTACK);
            add_to_top(s, rem);
        }
        // The remaining two combinations are the Java's no-ops: applyPowers finds
        // the marker already present, or removeSurroundedPower finds it already
        // absent. Both still ran `m.applyPowers()`, whose OTHER effect -- the
        // DamageInfo.output refresh -- this engine does not need, because the
        // multiplier is computed live at the damage site (back_attack.hpp note 3).
    }
}

void queue_pre_battle_back_attack_markers(CombatState& s) noexcept {
    // The Surrounded item this sits behind has NOT resolved yet, so the
    // hasPower conjunct is deliberately not tested: it is about to be true, which
    // is precisely what the game's onModifyPower fan-out observes when the item
    // ahead of this one lands (monster_spire_shield.hpp note 4).
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        if (!monster_back_attacked_by_position(s, i)) {
            continue;
        }
        if (actor_holds(s, i, PowerId::BACK_ATTACK)) {
            continue;  // `&& !this.hasPower("BackAttack")` (:1000)
        }
        ActionQueueItem apply{};
        apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        apply.src = i;
        apply.tgt = i;
        apply.amount = kBackAttackAppliedAmount;
        apply.flags = make_apply_power_flags(PowerId::BACK_ATTACK);
        add_to_bottom(s, apply);
    }
}

}  // namespace sts::engine
