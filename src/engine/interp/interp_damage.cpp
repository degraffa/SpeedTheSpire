// DAMAGE-domain opcode bodies -- the DamageInfo float pipeline and the two HP
// paths that land it (moved verbatim out of interp.cpp's anonymous namespace;
// see interp_ops.hpp for the split's rationale).
//
// Provenance: DamageInfo.java:35-100, MathUtils.java:217, StrengthPower.java:
// 92-98, VulnerablePower.java:61-73, WeakPower.java:61-70. Design doc §5.5, §6,
// §10 trap 1.

#include "interp_damage.hpp"

#include <cstdint>

#include "interp_ops.hpp"                   // actor_powers / actor_hp / actor_block
#include "sts/engine/action_queue.hpp"
#include "sts/engine/cards.hpp"             // CardId (Blood for Blood cost update)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"  // B3.17: on_monster_damaged
#include "sts/engine/power_hooks.hpp"       // B3.2 hook dispatch (wasHPLost/onAttacked)
#include "sts/engine/relic_hooks.hpp"       // B3.25: player_has_relic (Paper Phrog) + onMonsterDeath dispatch
#include "sts/engine/types.hpp"

namespace sts::engine {

namespace {

// --- Damage hooks (NORMAL damage only; skeleton powers) ---------------------
// Iteration order == power-list order == application order (design doc §4.1).
// Every hook is a float op mirroring the cited Java exactly.

// atDamageGive: attacker-owned hooks. StrengthPower.atDamageGive (+amount),
// WeakPower.atDamageGive (*0.75f). Others pass through. `strength_mult` scales the
// Strength contribution (Heavy Blade counts Strength x magicNumber by temporarily
// multiplying strength.amount before applyPowers; HeavyBlade.java:426-435). The
// default 1 is bit-identical to the pre-B3.3 hook: float(amount) * 1.0f == float(
// amount), so every non-Heavy-Blade damage number is unchanged.
[[nodiscard]] float at_damage_give(float dmg, PowerSlot p,
                                   int strength_mult = 1) noexcept {
    switch (static_cast<PowerId>(p.power_id)) {
        case PowerId::STRENGTH:                        // StrengthPower.java:96
            return dmg + static_cast<float>(p.amount) *
                             static_cast<float>(strength_mult);
        case PowerId::WEAK:
            return dmg * 0.75f;                         // WeakPower.java:67
        default:
            return dmg;
    }
}

// atDamageReceive: target-owned hooks. VulnerablePower.atDamageReceive: *1.5f,
// or *1.75f for a Vulnerable MONSTER when the player owns Paper Phrog
// (VulnerablePower.java:67-70 -- `!owner.isPlayer && player.hasRelic("Paper
// Frog")`; live as of B3.25, retiring A4.1's unreachable-branch note). The
// player-side Odd Mushroom *1.25f branch (:64-66) lands with its rare-relic
// owner (B3.26). Without Paper Phrog the 1.5f multiply is byte-identical to the
// pre-B3.25 hook (fixtures unchanged).
[[nodiscard]] float at_damage_receive(const CombatState& s, uint8_t owner_actor,
                                      float dmg, PowerSlot p) noexcept {
    switch (static_cast<PowerId>(p.power_id)) {
        case PowerId::VULNERABLE:
            if (owner_actor != kActorPlayer &&
                player_has_relic(s, RelicId::PAPER_PHROG)) {
                return dmg * 1.75f;                     // VulnerablePower.java:68
            }
            return dmg * 1.5f;                          // VulnerablePower.java:70
        default:
            return dmg;
    }
}

// atDamageFinalGive / atDamageFinalReceive: no skeleton power overrides these
// (Strength/Vulnerable/Weak leave AbstractPower's identity default), so both
// are pass-throughs. Kept as explicit call sites so a future power that hooks
// the "final" pass slots in without moving the pipeline.
[[nodiscard]] float at_damage_final_give(float dmg, PowerSlot /*p*/) noexcept {
    return dmg;
}
[[nodiscard]] float at_damage_final_receive(float dmg, PowerSlot /*p*/) noexcept {
    return dmg;
}

// Player stance hooks. The skeleton is stanceless (design doc §4.2: stance
// field is a "0 = None" placeholder), so both are documented identity stubs --
// written as real call sites so a future stance system attaches here without
// touching the pipeline shape.
[[nodiscard]] float stance_at_damage_give(const CombatState& /*s*/, float dmg) noexcept {
    return dmg;  // AbstractStance.atDamageGive default (no stance active)
}
[[nodiscard]] float stance_at_damage_receive(const CombatState& /*s*/, float dmg) noexcept {
    return dmg;  // AbstractStance.atDamageReceive default (no stance active)
}

void cards_took_player_damage(CombatState& s) noexcept;

[[nodiscard]] bool actor_has_power(const CombatState& s, uint8_t actor,
                                   PowerId id) noexcept {
    const PowerView pv = actor_powers(s, actor);
    for (uint8_t i = 0; i < pv.count; ++i) {
        if (pv.slots[i].power_id == static_cast<uint16_t>(id) &&
            pv.slots[i].amount > 0) {
            return true;
        }
    }
    return false;
}

// Blood for Blood.tookDamage: after each positive in-combat player HP-loss
// EVENT, updateCost(-1) on every copy in hand/discard/draw (but not exhaust or
// limbo/cardInUse). The reduction is per event, not per HP point, and clamps at
// zero. AbstractPlayer.updateCardsOnDamage (AbstractPlayer.java:1518-1530).
void cards_took_player_damage(CombatState& s) noexcept {
    auto update = [&](const CardPoolIndex* pile, uint8_t count) noexcept {
        for (uint8_t i = 0; i < count; ++i) {
            CardInstance& c = s.card_pool[pile[i]];
            if (c.card_id == static_cast<uint16_t>(CardId::BLOOD_FOR_BLOOD) &&
                c.cost_now > 0) {
                --c.cost_now;
            }
        }
    };
    update(s.hand, s.hand_count);
    update(s.discard, s.discard_count);
    update(s.draw, s.draw_count);
}

}  // namespace

// --- Opcode bodies ----------------------------------------------------------

// DAMAGE: compute output via the pipeline, then land it on tgt -- block absorbs
// first (decrementBlock: block soaks up to its value), remainder hits hp,
// currentHealth clamped >= 0. (Death/onDeath handling is not yet modeled; the
// pump's hp<=0 check drives the COMBAT_OVER transition.)
void op_damage(CombatState& s, uint8_t src, uint8_t tgt, int base,
               int strength_mult,
               DamageType type) noexcept {
    if (tgt != kActorPlayer && tgt >= kMonsterCap) {
        return;
    }
    // THORNS / HP_LOSS damage skips the NORMAL-only power pipeline (every skeleton
    // applyPowers hook is `if (type == NORMAL)` in the Java): a Vulnerable attacker
    // does NOT amplify reflected Thorns, and player Strength/Weak do not scale it.
    // NORMAL damage runs the full DamageInfo.applyPowers pipeline, carrying the
    // B3.3 strength multiplier (Heavy-Blade-style attacks).
    const int out = (type == DamageType::NORMAL)
                        ? compute_damage(s, src, tgt, base, strength_mult)
                        : (base < 0 ? 0 : base);
    int16_t* hp = actor_hp(s, tgt);
    int16_t* blk = actor_block(s, tgt);
    if (hp == nullptr || blk == nullptr) {
        return;
    }
    int dmg = out;
    int block = *blk;
    if (dmg >= block) {
        dmg -= block;
        block = 0;
    } else {
        block -= dmg;
        dmg = 0;
    }
    *blk = static_cast<int16_t>(block);
    // onAttacked (AbstractPlayer.damage:1425-1426): the VICTIM's powers fire on a
    // NORMAL attack from a DISTINCT attacker -- AFTER decrementBlock and REGARDLESS
    // of whether damage penetrated (Thorns reflects even a fully-blocked hit). A
    // THORNS/HP_LOSS incoming does NOT trigger onAttacked (ThornsPower's own type
    // guard), so it is dispatched only for NORMAL damage with src != tgt. No-op
    // unless a power binds ON_ATTACKED, so skeleton/relic-free DAMAGE is unchanged.
    if (type == DamageType::NORMAL && src != tgt) {
        dispatch_on_attacked(s, tgt, src, dmg);
    }
    const int old_hp = *hp;
    int new_hp = old_hp - dmg;
    if (new_hp < 0) {
        new_hp = 0;
    }
    *hp = static_cast<int16_t>(new_hp);
    // wasHPLost (AbstractPlayer.damage:1445-1447): fires on the VICTIM's powers
    // for the HP actually lost, with the ATTACKER as source. Rupture's guard
    // (source == victim) means unblocked enemy damage does NOT grant Strength --
    // only self-inflicted (card) HP loss does; Plated Armor's guard also reads the
    // damage `type` (it does not decrement on THORNS/HP_LOSS). No-op without those,
    // so skeleton DAMAGE is unchanged.
    const int hp_lost = old_hp - new_hp;
    dispatch_was_hp_lost(s, tgt, src, hp_lost, static_cast<uint8_t>(type));
    if (tgt == kActorPlayer && hp_lost > 0) {
        cards_took_player_damage(s);
    }
    // Monster death edge -> relics onMonsterDeath (AbstractMonster.die:933-937;
    // B3.25 Gremlin Horn). Fires once, when this hit drops the monster from
    // positive HP to 0. Runs BEFORE the damage() override seam below: die() is
    // called synchronously inside super.damage(), while the override's
    // post-super check sees isDying and never split-telegraphs a lethal hit.
    // No-op with an empty relic mirror (fixtures unchanged).
    if (tgt != kActorPlayer && old_hp > 0 && new_hp == 0) {
        const RelicView rv = player_relics(s);
        dispatch_relics_on_monster_death(s, rv.relics, rv.count, tgt);
    }
    // Monster damage() override seam (B3.17): the large slimes' split interrupt
    // wraps super.damage() and runs AFTER it, for EVERY DamageInfo type
    // (AcidSlime_L.java:142-152 / SpikeSlime_L.java:130-140 -- the guard reads
    // only resulting state, so a fully-blocked hit still checks). Dispatched by
    // monster_id; no-op for monsters without an override.
    if (tgt != kActorPlayer) {
        on_monster_damaged(s, tgt);
    }
}

// LOSE_HP: `tgt` loses `amount` HP directly, bypassing block (LoseHPAction /
// DamageInfo.HP_LOSS). Fires wasHPLost with source == tgt (SELF) -- the card /
// self HP-loss path that Rupture attributes to (Hemokinesis, Offering, Combust).
void op_lose_hp(CombatState& s, uint8_t tgt, int amount) noexcept {
    if (amount <= 0) {
        return;
    }
    int16_t* hp = actor_hp(s, tgt);
    if (hp == nullptr) {
        return;
    }
    const int old_hp = *hp;
    int new_hp = old_hp - amount;
    if (new_hp < 0) {
        new_hp = 0;
    }
    *hp = static_cast<int16_t>(new_hp);
    // source == victim (self), HP_LOSS type (bypasses block; not a THORNS/NORMAL
    // attack -- so it drives Rupture but not Thorns/Plated Armor's attack guards).
    const int hp_lost = old_hp - new_hp;
    dispatch_was_hp_lost(s, tgt, tgt, hp_lost,
                         static_cast<uint8_t>(DamageType::HP_LOSS));
    if (tgt == kActorPlayer && hp_lost > 0) {
        cards_took_player_damage(s);
    }
    // A direct HP loss can also kill a monster -> same die() relic dispatch
    // (AbstractMonster.die:933-937; B3.25), before the override seam as above.
    if (tgt != kActorPlayer && old_hp > 0 && new_hp == 0) {
        const RelicView rv = player_relics(s);
        dispatch_relics_on_monster_death(s, rv.relics, rv.count, tgt);
    }
    // LoseHPAction also routes through creature.damage() (LoseHPAction.java:41),
    // so the monster damage() override seam fires here too (B3.17).
    if (tgt != kActorPlayer) {
        on_monster_damaged(s, tgt);
    }
}

// DropkickAction.update: test Vulnerable when the action resolves. Damage is
// first; if the condition was true, GainEnergyAction then DrawCardAction follow.
void op_dropkick(CombatState& s, const ActionQueueItem& item) noexcept {
    const bool vulnerable = actor_has_power(s, item.tgt, PowerId::VULNERABLE);
    op_damage(s, item.src, item.tgt, item.amount);
    if (!vulnerable) {
        return;
    }
    ActionQueueItem energy{};
    energy.opcode = static_cast<uint16_t>(Opcode::GAIN_ENERGY);
    energy.src = kActorPlayer;
    energy.tgt = kActorPlayer;
    energy.amount = 1;
    add_to_bottom(s, energy);
    ActionQueueItem draw{};
    draw.opcode = static_cast<uint16_t>(Opcode::DRAW);
    draw.src = kActorPlayer;
    draw.tgt = kActorPlayer;
    draw.amount = 1;
    add_to_bottom(s, draw);
}

// --- Public: DAMAGE pipeline -------------------------------------------------

int compute_damage(const CombatState& s, uint8_t src, uint8_t tgt,
                   int base) noexcept {
    return compute_damage(s, src, tgt, base, /*strength_mult=*/1);
}

int compute_damage(const CombatState& s, uint8_t src, uint8_t tgt, int base,
                   int strength_mult) noexcept {
    const PowerView owner = actor_powers(s, src);
    const PowerView target = actor_powers(s, tgt);
    float tmp = static_cast<float>(base);

    if (src != kActorPlayer) {
        // Monster-owned attack (DamageInfo.java:39-70). Target is (for the
        // skeleton) the player, so the stance hook is atDamageReceive and sits
        // AFTER the target's atDamageReceive loop. (strength_mult is only ever != 1
        // for the player's Heavy Blade, so it is a no-op on this branch.)
        for (uint8_t i = 0; i < owner.count; ++i) {
            tmp = at_damage_give(tmp, owner.slots[i], strength_mult);
        }
        for (uint8_t i = 0; i < target.count; ++i) {
            tmp = at_damage_receive(s, tgt, tmp, target.slots[i]);
        }
        tmp = stance_at_damage_receive(s, tmp);
        for (uint8_t i = 0; i < owner.count; ++i) {
            tmp = at_damage_final_give(tmp, owner.slots[i]);
        }
        for (uint8_t i = 0; i < target.count; ++i) {
            tmp = at_damage_final_receive(tmp, target.slots[i]);
        }
    } else {
        // Player-owned attack (DamageInfo.java:71-99). Owner is the player, so
        // the stance hook is atDamageGive and sits AFTER the owner's
        // atDamageGive loop, BEFORE the target's atDamageReceive loop.
        for (uint8_t i = 0; i < owner.count; ++i) {
            tmp = at_damage_give(tmp, owner.slots[i], strength_mult);
        }
        tmp = stance_at_damage_give(s, tmp);
        for (uint8_t i = 0; i < target.count; ++i) {
            tmp = at_damage_receive(s, tgt, tmp, target.slots[i]);
        }
        for (uint8_t i = 0; i < owner.count; ++i) {
            tmp = at_damage_final_give(tmp, owner.slots[i]);
        }
        for (uint8_t i = 0; i < target.count; ++i) {
            tmp = at_damage_final_receive(tmp, target.slots[i]);
        }
    }

    int out = mathutils_floor(tmp);  // one floor, at the very end (trap 1)
    if (out < 0) {
        out = 0;
    }
    return out;
}

}  // namespace sts::engine
