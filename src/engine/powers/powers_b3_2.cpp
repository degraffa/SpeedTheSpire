// B3.2 framework powers -- native hook bodies (moved verbatim out of
// power_hooks.cpp's escape-hatch switch; see powers_b3_2.hpp for the batch scope
// and power_native.hpp for the split's rationale).

#include "powers_b3_2.hpp"

#include <cstdint>

#include "power_native.hpp"             // find_power
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/cards.hpp"         // card_def, CardType (Corruption skill check)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags, CardPile
#include "sts/engine/powers.hpp"        // power_def, PowerDef, PowerType
#include "sts/engine/types.hpp"

namespace sts::engine {

// Artifact -- DELIBERATELY EMPTY, not a missing body.
//
// ArtifactPower's only effect is the target-side nullify: ApplyPowerAction
// (ApplyPowerAction.java:106-138) consumes one Artifact stack and drops the
// incoming DEBUFF. That is implemented inline at the APPLY_POWER site, in
// apply_power_blocked_by_artifact (power_hooks.cpp), because it must answer
// "was the debuff blocked?" -- something a queue-only hook body cannot do. So
// Artifact has no source-side / per-power-list hook body, even though its
// registry row is `native: true` and lists on_apply_power (so the row's hook
// inventory still mirrors the Java power).
//
// This empty definition IS the record of that decision. The generated dispatch
// table (STS_REGISTRY_NATIVE_POWERS, expanded in power_hooks.cpp) odr-uses a
// handler for every `native: true` row, so omitting this function would be an
// undefined reference at link time rather than a silent no-op; writing it
// deliberately is how a "native, no handler" row is expressed. Behaviour is
// identical to the old `case PowerId::ARTIFACT: return nullptr;` --
// dispatch_native_hook either skips a null pointer or calls a body that does
// nothing.
void power_native_artifact(CombatState& /*s*/, Hook /*hook*/,
                           const HookContext& /*ctx*/) noexcept {}

void power_native_sadistic(CombatState& s, Hook hook,
                           const HookContext& ctx) noexcept {
    // SadisticPower.onApplyPower (source side): on applying a DEBUFF to a
    // DIFFERENT creature that has no Artifact, deal `amount` THORNS damage
    // to that target. (Shackled excluded; no Shackled power in scope.)
    if (hook != Hook::ON_APPLY_POWER) {
        return;
    }
    const PowerId applied =
        static_cast<PowerId>(ctx.applied_power_id);
    const PowerDef* ap = power_def(applied);
    const bool is_debuff = ap != nullptr && ap->type == PowerType::DEBUFF;
    if (!is_debuff || ctx.target == ctx.owner) {
        return;
    }
    if (find_power(s, ctx.target, PowerId::ARTIFACT) != nullptr) {
        return;  // Artifact target -> Sadistic skips (its own guard)
    }
    ActionQueueItem dmg{};
    dmg.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    dmg.src = ctx.owner;         // owner-owned THORNS damage
    dmg.tgt = ctx.target;
    dmg.amount = ctx.power_amount;
    add_to_bottom(s, dmg);       // addToBot (SadisticPower.java:43)
}

void power_native_dark_embrace(CombatState& s, Hook hook,
                               const HookContext& ctx) noexcept {
    // DarkEmbracePower.onExhaust (Java:36-41) first checks
    // areMonstersBasicallyDead(); an exhaust after combat cannot draw.
    if (hook != Hook::ON_EXHAUST) {
        return;
    }
    bool any_live = false;
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        any_live = any_live || s.monsters[i].hp > 0;
    }
    if (!any_live) {
        return;
    }
    ActionQueueItem draw{};
    draw.opcode = static_cast<uint16_t>(Opcode::DRAW);
    draw.src = ctx.owner;
    draw.tgt = ctx.owner;
    draw.amount = ctx.power_amount;
    add_to_bottom(s, draw);
}

void power_native_combust(CombatState& s, Hook hook,
                          const HookContext& ctx) noexcept {
    // CombustPower.atEndOfTurn (CombustPower.java:39-47): enqueue its
    // private hpLoss first, then THORNS damage to every enemy. stackPower
    // increments hpLoss once per reapplication; the player-only card path
    // persists that hidden counter in CombatState.flags.
    if (hook != Hook::AT_END_OF_TURN) {
        return;
    }
    bool any_live = false;
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        any_live = any_live || s.monsters[i].hp > 0;
    }
    if (!any_live) {
        return;
    }
    uint32_t hp_loss =
        (s.flags & kCombatFlagCombustHpLossMask) >> kCombatFlagCombustHpLossShift;
    if (hp_loss == 0u) {
        hp_loss = 1u;  // constructed fixture/direct power application
    }
    ActionQueueItem lose{};
    lose.opcode = static_cast<uint16_t>(Opcode::LOSE_HP);
    lose.src = ctx.owner;
    lose.tgt = ctx.owner;
    lose.amount = static_cast<int32_t>(hp_loss);
    add_to_bottom(s, lose);
    ActionQueueItem damage{};
    damage.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    damage.src = ctx.owner;
    damage.tgt = (ctx.owner == kActorPlayer) ? kActorAllEnemies : kActorPlayer;
    damage.amount = ctx.power_amount;
    damage.flags = make_damage_flags(DamageType::THORNS);
    add_to_bottom(s, damage);
}

void power_native_rupture(CombatState& s, Hook hook,
                          const HookContext& ctx) noexcept {
    // RupturePower.wasHPLost: fire ONLY when the HP loss was self-inflicted
    // (info.owner == owner) -- card HP loss, not unblocked enemy damage.
    if (hook != Hook::WAS_HP_LOST) {
        return;
    }
    if (ctx.source != ctx.owner || ctx.amount <= 0) {
        return;  // attribution guard (RupturePower.java:61)
    }
    ActionQueueItem gain{};
    gain.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    gain.src = ctx.owner;
    gain.tgt = ctx.owner;
    gain.amount = ctx.power_amount;  // +amount Strength
    gain.flags = make_apply_power_flags(PowerId::STRENGTH);
    add_to_top(s, gain);             // addToTop (RupturePower.java:63)
}

void power_native_corruption(CombatState& s, Hook hook,
                             const HookContext& ctx) noexcept {
    // CorruptionPower: a played SKILL is redirected to exhaust (onUseCard);
    // a drawn SKILL costs 0 this turn (onCardDraw). Both key off the card
    // being a SKILL. The pool index identifies the specific instance.
    if (ctx.card_pool_index >= kCardPoolCap) {
        return;
    }
    const CardId cid = static_cast<CardId>(ctx.card_id);
    const CardDef* cd = card_def(cid);
    if (cd == nullptr || cd->type != CardType::SKILL) {
        return;
    }
    if (hook == Hook::ON_USE_CARD) {
        // card.exhaustOnUseOnce: mark this instance to exhaust on play.
        s.card_pool[ctx.card_pool_index].flags |=
            card_flag_bit(CardFlag::EXHAUST);
    } else if (hook == Hook::ON_CARD_DRAW) {
        s.card_pool[ctx.card_pool_index].cost_now = 0;  // setCostForTurn(0)
    }
}

void power_native_rage(CombatState& s, Hook hook,
                       const HookContext& ctx) noexcept {
    // RagePower (B3.6 completes the B3.2 stub). onUseCard (RagePower.
    // java:41-47): if the played card is an ATTACK, GainBlockAction(
    // player, amount) -- dispatched at ON_USE_CARD (after the card's own
    // effects are queued, so the block lands after the attack's damage;
    // a direct GainBlockAction -> kBlockNoPowers, no Dexterity/Frail).
    // atEndOfTurn (:49-52): addToBot RemoveSpecificPowerAction(owner,
    // "Rage") -- Rage lasts one turn.
    if (hook == Hook::ON_USE_CARD) {
        const CardDef* cd = card_def(static_cast<CardId>(ctx.card_id));
        if (cd == nullptr || cd->type != CardType::ATTACK) {
            return;  // the attack-type guard (RagePower.java:42)
        }
        ActionQueueItem blk{};
        blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
        blk.src = ctx.owner;
        blk.tgt = ctx.owner;  // owner == the player in every S1 scope
        blk.amount = ctx.power_amount;
        blk.flags = kBlockNoPowers;
        add_to_bottom(s, blk);  // addToBot (RagePower.java:43)
        return;
    }
    if (hook == Hook::AT_END_OF_TURN) {
        ActionQueueItem rem{};
        rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
        rem.src = ctx.owner;
        rem.tgt = ctx.owner;
        rem.flags = make_apply_power_flags(PowerId::RAGE);
        add_to_bottom(s, rem);  // addToBot (RagePower.java:50)
        return;
    }
}

}  // namespace sts::engine
