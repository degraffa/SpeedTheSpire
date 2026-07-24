// Potion-support-powers follow-up batch -- native hook bodies (moved verbatim out
// of power_hooks.cpp's escape-hatch switch; see power_native.hpp for the split's
// rationale).

#include "powers_potion_support.hpp"

#include <cstdint>

#include "power_native.hpp"             // find_power
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_thorns(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept {
    // ThornsPower.onAttacked (ThornsPower.java:45-52): reflect `amount`
    // THORNS damage back to the attacker. op_damage already gated this to a
    // NORMAL attack from a distinct creature; the owner != attacker guard is
    // re-checked. THORNS type -> the reflected DAMAGE skips all NORMAL-only
    // power modifiers, so a Vulnerable attacker does NOT amplify it.
    if (hook != Hook::ON_ATTACKED || ctx.source == ctx.owner) {
        return;
    }
    ActionQueueItem dmg{};
    dmg.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    dmg.src = ctx.owner;        // the thorns-haver owns the reflected damage
    dmg.tgt = ctx.source;       // ... dealt to the attacker
    dmg.amount = ctx.power_amount;
    dmg.flags = make_damage_flags(DamageType::THORNS);
    add_to_top(s, dmg);         // addToTop (ThornsPower.java:48)
}

void power_native_plated_armor(CombatState& s, Hook hook,
                               const HookContext& ctx) noexcept {
    if (hook == Hook::AT_END_OF_TURN_PRE_CARD) {
        // GainBlockAction(owner, amount) at the §5.4 pre-card phase (the same
        // slot as Metallicize). A direct GainBlockAction -> kBlockNoPowers, so
        // Plated Armor block does NOT get Dexterity (PlatedArmorPower.java:72).
        ActionQueueItem blk{};
        blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
        blk.src = ctx.owner;
        blk.tgt = ctx.owner;
        blk.amount = ctx.power_amount;
        blk.flags = kBlockNoPowers;
        add_to_bottom(s, blk);  // addToBot (PlatedArmorPower.java:72)
        return;
    }
    if (hook == Hook::WAS_HP_LOST) {
        // Lose 1 stack on a real attack from a distinct creature; NOT on a
        // THORNS / HP_LOSS / self loss (PlatedArmorPower.java:54-58). The
        // ReducePowerAction removes the power at 0.
        if (ctx.damage_type == static_cast<uint8_t>(DamageType::THORNS) ||
            ctx.damage_type == static_cast<uint8_t>(DamageType::HP_LOSS) ||
            ctx.source == ctx.owner || ctx.amount <= 0) {
            return;
        }
        PowerSlot* pa = find_power(s, ctx.owner, PowerId::PLATED_ARMOR);
        if (pa != nullptr) {
            pa->amount = static_cast<int16_t>(pa->amount - 1);
            if (pa->amount <= 0) {
                pa->power_id = static_cast<uint16_t>(PowerId::NONE);
            }
        }
        return;
    }
}

void power_native_regen(CombatState& s, Hook hook,
                        const HookContext& ctx) noexcept {
    // RegenPower.atEndOfTurn -> RegenAction(owner, amount)
    // (RegenPower.java:35-38, RegenAction.java:34-47): heal `amount` (clamped
    // to max, only if currentHealth>0) then, for a PLAYER owner, decrement the
    // stack by 1 (remove at 0). The heal is applied directly -- no HEAL opcode
    // (the Blood Potion / Burning Blood precedent) and a heal has no queue
    // interplay with other end-of-turn effects.
    if (hook != Hook::AT_END_OF_TURN) {
        return;
    }
    int16_t* hp = nullptr;
    int16_t max_hp = 0;
    if (ctx.owner == kActorPlayer) {
        hp = &s.player_hp;
        max_hp = s.player_max_hp;
    } else if (ctx.owner < kMonsterCap) {
        hp = &s.monsters[ctx.owner].hp;
        max_hp = s.monsters[ctx.owner].max_hp;
    }
    if (hp != nullptr && *hp > 0) {
        int32_t v = static_cast<int32_t>(*hp) + ctx.power_amount;
        if (v > max_hp) {
            v = max_hp;
        }
        *hp = static_cast<int16_t>(v);
    }
    if (ctx.owner == kActorPlayer) {  // RegenAction decrement is isPlayer-gated
        PowerSlot* rp = find_power(s, ctx.owner, PowerId::REGEN);
        if (rp != nullptr) {
            rp->amount = static_cast<int16_t>(rp->amount - 1);
            if (rp->amount <= 0) {
                rp->power_id = static_cast<uint16_t>(PowerId::NONE);
            }
        }
    }
}

void power_native_lose_dexterity(CombatState& s, Hook hook,
                                 const HookContext& ctx) noexcept {
    // LoseDexterityPower.atEndOfTurn (LoseDexterityPower.java:38-42): addToBot
    // ApplyPower(Dexterity, -amount) then RemoveSpecificPower(self) -- the
    // exact mirror of LoseStrength/Flex (id 13). Both queued (addToBot), so the
    // Dexterity reduction + self-removal resolve on later pump iterations.
    if (hook != Hook::AT_END_OF_TURN) {
        return;
    }
    ActionQueueItem down{};
    down.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    down.src = ctx.owner;
    down.tgt = ctx.owner;
    down.amount = -ctx.power_amount;  // Dexterity -amount
    down.flags = make_apply_power_flags(PowerId::DEXTERITY);
    add_to_bottom(s, down);
    ActionQueueItem rem{};
    rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
    rem.src = ctx.owner;
    rem.tgt = ctx.owner;
    rem.flags = make_apply_power_flags(PowerId::LOSE_DEXTERITY);
    add_to_bottom(s, rem);
}

void power_native_ritual(CombatState& s, Hook hook,
                         const HookContext& ctx) noexcept {
    // RitualPower has two branches on `onPlayer`; we key it on the OWNER
    // actor (the potion applies to the PLAYER; the Cultist to ITSELF):
    //   * atEndOfTurn (RitualPower.java:38-43, isPlayer guard): the
    //     player/potion Ritual gains `amount` Strength each end of turn.
    //     Only the player is dispatched AT_END_OF_TURN (dispatch_at_end_of_
    //     turn), and a monster owner is guarded out here for safety.
    //   * atEndOfRound (:46-55, !onPlayer + skipFirst): the monster Cultist
    //     Ritual gains `amount` Strength each round AFTER the first. The
    //     skipFirst state is the owner monster's kMonsterFlagRitualSkip bit,
    //     set by the Cultist when it casts Incantation.
    if (hook == Hook::AT_END_OF_TURN) {
        if (ctx.owner != kActorPlayer) {
            return;  // RitualPower.atEndOfTurn's isPlayer guard
        }
        ActionQueueItem up{};
        up.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        up.src = ctx.owner;
        up.tgt = ctx.owner;
        up.amount = ctx.power_amount;  // +amount Strength
        up.flags = make_apply_power_flags(PowerId::STRENGTH);
        add_to_bottom(s, up);          // addToBot (RitualPower.java:41)
        return;
    }
    if (hook == Hook::AT_END_OF_ROUND) {
        if (ctx.owner == kActorPlayer || ctx.owner >= kMonsterCap) {
            return;  // player Ritual is onPlayer -> atEndOfRound no-op
        }
        uint16_t& mf = s.monsters[ctx.owner].flags;
        if ((mf & kMonsterFlagRitualSkip) != 0u) {
            mf = static_cast<uint16_t>(mf & ~kMonsterFlagRitualSkip);
            return;  // skipFirst: consume, no Strength this round
        }
        ActionQueueItem up{};
        up.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        up.src = ctx.owner;
        up.tgt = ctx.owner;
        up.amount = ctx.power_amount;  // +amount Strength
        up.flags = make_apply_power_flags(PowerId::STRENGTH);
        add_to_bottom(s, up);          // addToBot (RitualPower.java:50)
        return;
    }
}

void power_native_curl_up(CombatState& s, Hook hook,
                          const HookContext& ctx) noexcept {
    // CurlUpPower.onAttacked (CurlUpPower.java:36-46): the FIRST NORMAL,
    // non-lethal (damageAmount < owner.currentHealth), >0 attack makes the
    // louse gain `amount` Block, then removes Curl Up (one-shot -- modelled
    // by the self-removal). op_damage already gated dispatch_on_attacked to
    // NORMAL src != tgt AFTER decrementBlock, with the post-block damage and
    // the owner's pre-hit HP, so `ctx.amount` == damageAmount and the owner's
    // hp is still currentHealth here.
    if (hook != Hook::ON_ATTACKED) {
        return;
    }
    if (ctx.source == ctx.owner || ctx.amount <= 0 ||
        ctx.owner >= kMonsterCap) {
        return;
    }
    MonsterState& owner = s.monsters[ctx.owner];
    if ((owner.flags & kMonsterFlagCurlUpTriggered) != 0u) {
        return;  // triggered latch flips before queued actions resolve
    }
    if (ctx.amount >= owner.hp) {
        return;  // damageAmount < owner.currentHealth guard (lethal skips)
    }
    // CurlUpPower.java:40 sets triggered=true synchronously, before the
    // GainBlock/RemoveSpecificPower actions it adds to the bottom. This is
    // observable for queued multi-hit attacks: later hits run while Curl Up
    // is still present but must not queue another block gain.
    owner.flags |= kMonsterFlagCurlUpTriggered;
    ActionQueueItem blk{};
    blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
    blk.src = ctx.owner;
    blk.tgt = ctx.owner;
    blk.amount = ctx.power_amount;  // Block = Curl Up amount
    blk.flags = kBlockNoPowers;     // GainBlockAction (direct, no Dexterity)
    add_to_bottom(s, blk);          // addToBot (CurlUpPower.java:42)
    ActionQueueItem rem{};
    rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
    rem.src = ctx.owner;
    rem.tgt = ctx.owner;
    rem.flags = make_apply_power_flags(PowerId::CURL_UP);
    add_to_bottom(s, rem);          // addToBot (CurlUpPower.java:43)
}

}  // namespace sts::engine
