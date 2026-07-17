// Effect interpreter -- opcode dispatch + the DAMAGE pipeline. See interp.hpp
// for the full design/provenance notes and the scope-boundary/field-encoding
// decisions (SHUFFLE_IN & ROLL_MOVE stubs, NOP == 0, APPLY_POWER flags
// encoding, EXHAUST via `amount` pool index, kActorPlayer actor sentinel).
//
// Provenance: DamageInfo.java:35-100, MathUtils.java:217, StrengthPower.java:
// 92-98, VulnerablePower.java:61-73, WeakPower.java:61-70. Design doc §5.5, §6,
// §10 trap 1.

#include "sts/engine/interp.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/piles.hpp"   // draw_cards / shuffle_discard_into_draw / exhaust_card
#include "sts/engine/types.hpp"

namespace sts::engine {

namespace {

// --- Actor power views ------------------------------------------------------

struct PowerView {
    const PowerSlot* slots;  // may be null when actor is out of range
    uint8_t count;
};

// Powers of an actor (kActorPlayer -> player_powers, 0..4 -> that monster).
// Out-of-range actors yield an empty view so callers iterate nothing and never
// index OOB (defensive against junk src/tgt in malformed items).
[[nodiscard]] PowerView actor_powers(const CombatState& s, uint8_t actor) noexcept {
    if (actor == kActorPlayer) {
        return PowerView{s.player_powers, s.player_power_count};
    }
    if (actor < kMonsterCap) {
        return PowerView{s.monsters[actor].powers, s.monsters[actor].power_count};
    }
    return PowerView{nullptr, 0};
}

// Mutable hp/block cells for an actor (null when out of range).
[[nodiscard]] int16_t* actor_hp(CombatState& s, uint8_t actor) noexcept {
    if (actor == kActorPlayer) {
        return &s.player_hp;
    }
    if (actor < kMonsterCap) {
        return &s.monsters[actor].hp;
    }
    return nullptr;
}

[[nodiscard]] int16_t* actor_block(CombatState& s, uint8_t actor) noexcept {
    if (actor == kActorPlayer) {
        return &s.player_block;
    }
    if (actor < kMonsterCap) {
        return &s.monsters[actor].block;
    }
    return nullptr;
}

// --- Damage hooks (NORMAL damage only; skeleton powers) ---------------------
// Iteration order == power-list order == application order (design doc §4.1).
// Every hook is a float op mirroring the cited Java exactly.

// atDamageGive: attacker-owned hooks. StrengthPower.atDamageGive (+amount),
// WeakPower.atDamageGive (*0.75f). Others pass through.
[[nodiscard]] float at_damage_give(float dmg, PowerSlot p) noexcept {
    switch (static_cast<PowerId>(p.power_id)) {
        case PowerId::STRENGTH:
            return dmg + static_cast<float>(p.amount);  // StrengthPower.java:96
        case PowerId::WEAK:
            return dmg * 0.75f;                         // WeakPower.java:67
        default:
            return dmg;
    }
}

// atDamageReceive: target-owned hooks. VulnerablePower.atDamageReceive (*1.5f).
[[nodiscard]] float at_damage_receive(float dmg, PowerSlot p) noexcept {
    switch (static_cast<PowerId>(p.power_id)) {
        case PowerId::VULNERABLE:
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

// --- Opcode bodies ----------------------------------------------------------

// DAMAGE: compute output via the pipeline, then land it on tgt -- block absorbs
// first (decrementBlock: block soaks up to its value), remainder hits hp,
// currentHealth clamped >= 0. (Death/onDeath handling is not yet modeled; the
// pump's hp<=0 check drives the COMBAT_OVER transition.)
void op_damage(CombatState& s, uint8_t src, uint8_t tgt, int base) noexcept {
    if (tgt != kActorPlayer && tgt >= kMonsterCap) {
        return;
    }
    const int out = compute_damage(s, src, tgt, base);
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
    int new_hp = *hp - dmg;
    if (new_hp < 0) {
        new_hp = 0;
    }
    *hp = static_cast<int16_t>(new_hp);
}

// BLOCK: tgt gains `amount` block. Skeleton path is a straight add -- in the
// base game relic onPlayerGainedBlock / power onGainedBlock would hook here, but
// none of Strength/Vulnerable/Weak (and no relic) touches block gain in this
// version, and the skeleton has no relics/Dexterity/Frail. (< 0 clamped so a
// negative amount can't drive block below zero.)
void op_block(CombatState& s, uint8_t tgt, int amount) noexcept {
    int16_t* blk = actor_block(s, tgt);
    if (blk == nullptr) {
        return;
    }
    int nb = *blk + amount;  // future: relic/power onGainedBlock hooks go here
    if (nb < 0) {
        nb = 0;
    }
    *blk = static_cast<int16_t>(nb);
}

// APPLY_POWER: stack PowerId(flags) x amount onto tgt. Stacks onto an existing
// slot of the same id, else appends a new slot (hard cap kPowerCap -- overflow
// is a silent no-op here rather than an assert, since a malformed item must not
// crash; real card play never overflows 24 skeleton powers).
void op_apply_power(CombatState& s, uint8_t tgt, PowerId id, int amount) noexcept {
    if (id == PowerId::NONE) {
        return;
    }
    PowerSlot* slots = nullptr;
    uint8_t* count = nullptr;
    if (tgt == kActorPlayer) {
        slots = s.player_powers;
        count = &s.player_power_count;
    } else if (tgt < kMonsterCap) {
        slots = s.monsters[tgt].powers;
        count = &s.monsters[tgt].power_count;
    } else {
        return;
    }
    const uint16_t pid = static_cast<uint16_t>(id);
    for (uint8_t i = 0; i < *count; ++i) {
        if (slots[i].power_id == pid) {
            slots[i].amount = static_cast<int16_t>(slots[i].amount + amount);
            return;
        }
    }
    if (*count >= kPowerCap) {
        return;
    }
    slots[*count].power_id = pid;
    slots[*count].amount = static_cast<int16_t>(amount);
    ++*count;
}

// DRAW and EXHAUST (and SHUFFLE_IN) are implemented in piles.cpp
// (draw_cards / exhaust_card / shuffle_discard_into_draw); the dispatch below
// delegates to them.

}  // namespace

// --- Public: DAMAGE pipeline -------------------------------------------------

int compute_damage(const CombatState& s, uint8_t src, uint8_t tgt,
                   int base) noexcept {
    const PowerView owner = actor_powers(s, src);
    const PowerView target = actor_powers(s, tgt);
    float tmp = static_cast<float>(base);

    if (src != kActorPlayer) {
        // Monster-owned attack (DamageInfo.java:39-70). Target is (for the
        // skeleton) the player, so the stance hook is atDamageReceive and sits
        // AFTER the target's atDamageReceive loop.
        for (uint8_t i = 0; i < owner.count; ++i) {
            tmp = at_damage_give(tmp, owner.slots[i]);
        }
        for (uint8_t i = 0; i < target.count; ++i) {
            tmp = at_damage_receive(tmp, target.slots[i]);
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
            tmp = at_damage_give(tmp, owner.slots[i]);
        }
        tmp = stance_at_damage_give(s, tmp);
        for (uint8_t i = 0; i < target.count; ++i) {
            tmp = at_damage_receive(tmp, target.slots[i]);
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

// --- Public: dispatch --------------------------------------------------------

void execute_opcode(CombatState& s, const ActionQueueItem& item) noexcept {
    switch (static_cast<Opcode>(item.opcode)) {
        case Opcode::NOP:
            return;  // reserved safe no-op (value-init / unrecognized item)
        case Opcode::DAMAGE:
            op_damage(s, item.src, item.tgt, item.amount);
            return;
        case Opcode::BLOCK:
            op_block(s, item.tgt, item.amount);
            return;
        case Opcode::APPLY_POWER:
            op_apply_power(s, item.tgt, apply_power_id_from_flags(item.flags),
                           item.amount);
            return;
        case Opcode::DRAW:
            (void)draw_cards(s, item.amount);  // piles.cpp: cap + reshuffle
            return;
        case Opcode::GAIN_ENERGY:
            // player_energy += amount; no max-energy field to clamp against
            // (Ironclad base 3 is a constant, no relic/potion raises it here).
            s.player_energy = static_cast<int16_t>(s.player_energy + item.amount);
            return;
        case Opcode::SHUFFLE_IN:
            shuffle_discard_into_draw(s);  // piles.cpp: shuffle_rng + JDK LCG
            return;
        case Opcode::EXHAUST:
            exhaust_card(s, item.amount);  // piles.cpp
            return;
        case Opcode::ROLL_MOVE:
            return;  // stub: Jaw Worm rolls inside its MonsterTurnFn
        default:
            return;  // any unrecognized opcode is a safe no-op (decision (3))
    }
}

}  // namespace sts::engine
