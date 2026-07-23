// Power-hook framework -- dispatch in the frozen stage-a §5.2/§5.3/§5.4/§5.5
// order + the native escape hatch. See power_hooks.hpp for the full hook
// inventory, the per-hook source orders (verified against the decompiled Java),
// and the regression invariant (no-op when no hook-bearing power is present).
//
// Provenance (read in full before coding): GameActionManager.getNextAction
// (:222-245 onPlayCard fan-out; :369-377 callEndOfTurnActions), UseCardAction
// (:41-64 onUseCard fan-out), CardGroup.moveToExhaustPile (:851-856 onExhaust),
// ApplyPowerAction.update (:106-138 onApplyPower source + Artifact nullify),
// AbstractPlayer.damage (:1445-1447 wasHPLost), AbstractCreature.addBlock
// (:426-433 onGainedBlock) / applyEndOfTurnTriggers (:548-553 atEndOfTurn),
// AbstractRoom.applyEndOfTurnPreCardPowers (:535-539). Power bodies: FeelNoPain/
// DarkEmbrace/Metallicize/Combust/Rupture/Sadistic/Corruption/Artifact/Rage
// (cited per-entry in registry/powers.yaml). Design doc §5.2-5.5.

#include "sts/engine/power_hooks.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/cards.hpp"         // card_def, CardType (Corruption skill check)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags, CardPile
#include "sts/engine/powers.hpp"        // power_def, PowerDef, PowerType
#include "sts/engine/relic_hooks.hpp"   // relic dispatch (B3.24; the acq-order sites)
#include "sts/engine/types.hpp"

namespace sts::engine {

namespace {

// Resolve a power-hook step's target relative to the power's OWNER and queue it.
//   * SELF          -> the owner actor.
//   * ALL_ENEMY     -> the owner's enemies (kActorAllEnemies for a player owner;
//                      the player for a monster owner).
//   * RANDOM_ENEMY  -> likewise, one random enemy.
//   * CARD_TARGET   -> ctx.target (the card/apply target; rare for powers).
// `src` is the owner (so a DAMAGE from a hook takes the owner's ownership branch
// and an APPLY_POWER's source is the owner). AMOUNT convention: step.amount == 0
// substitutes the power's stack amount (ctx.power_amount); non-zero is literal.
void queue_hook_step(CombatState& s, uint8_t owner, const CardEffectStep& step,
                     const HookContext& ctx) noexcept {
    ActionQueueItem item{};
    item.opcode = static_cast<uint16_t>(step.op);
    item.src = owner;
    switch (step.target) {
        case StepTarget::SELF:
            item.tgt = owner;
            break;
        case StepTarget::ALL_ENEMY:
            item.tgt = (owner == kActorPlayer) ? kActorAllEnemies : kActorPlayer;
            break;
        case StepTarget::RANDOM_ENEMY:
            item.tgt = (owner == kActorPlayer) ? kActorRandomEnemy : kActorPlayer;
            break;
        case StepTarget::CARD_TARGET:
            item.tgt = ctx.target;
            break;
        default:
            item.tgt = owner;
            break;
    }
    item.amount = (step.amount == 0) ? ctx.power_amount : step.amount;
    item.flags = step.extra;  // APPLY_POWER: PowerId; else 0
    add_to_bottom(s, item);
}

// The power-slot list for an actor (kActorPlayer -> player_powers; a monster slot
// -> that monster's powers). Out-of-range -> empty.
struct PowerListView {
    PowerSlot* slots;
    uint8_t count;
};
[[nodiscard]] PowerListView actor_power_list(CombatState& s, uint8_t actor) noexcept {
    if (actor == kActorPlayer) {
        return PowerListView{s.player_powers, s.player_power_count};
    }
    if (actor < kMonsterCap) {
        return PowerListView{s.monsters[actor].powers, s.monsters[actor].power_count};
    }
    return PowerListView{nullptr, 0};
}

// Dispatch one hook over one actor's power list, in power-list == application
// order (§5.5). Each responding power (a binding for `hook` exists) either runs
// its data program (queue its steps) or routes to the native escape hatch.
void dispatch_actor_powers(CombatState& s, uint8_t owner, Hook hook,
                           const HookContext& base) noexcept {
    const PowerListView pv = actor_power_list(s, owner);
    for (uint8_t i = 0; i < pv.count; ++i) {
        const PowerId pid = static_cast<PowerId>(pv.slots[i].power_id);
        if (pid == PowerId::NONE) {
            continue;
        }
        const PowerDef* def = power_def(pid);
        if (def == nullptr) {
            continue;
        }
        // engine::Hook and registry::Hook are pinned byte-equal (powers.hpp);
        // the generated hook_binding takes the registry mirror.
        const PowerHookBinding* b =
            def->hook_binding(static_cast<sts::registry::Hook>(hook));
        if (b == nullptr) {
            continue;  // this power does not respond to this hook
        }
        HookContext ctx = base;
        ctx.owner = owner;
        ctx.power_amount = pv.slots[i].amount;
        if (def->native) {
            dispatch_native_hook(s, hook, pid, ctx);
        } else {
            for (uint8_t k = 0; k < b->step_count; ++k) {
                queue_hook_step(s, owner, b->steps[k], ctx);
            }
        }
    }
}

// Does `actor` currently hold `pid` (with a live slot)? Also returns the slot for
// mutation (Artifact consume).
[[nodiscard]] PowerSlot* find_power(CombatState& s, uint8_t actor,
                                    PowerId pid) noexcept {
    const PowerListView pv = actor_power_list(s, actor);
    for (uint8_t i = 0; i < pv.count; ++i) {
        if (pv.slots[i].power_id == static_cast<uint16_t>(pid)) {
            return &pv.slots[i];
        }
    }
    return nullptr;
}

}  // namespace

// --- Card-play fan-outs ------------------------------------------------------

void dispatch_on_play_card(CombatState& s, uint16_t card_id,
                           uint8_t target) noexcept {
    HookContext ctx{};
    ctx.card_id = card_id;
    ctx.target = target;
    // §5.3 (GameActionManager.java:222-245): player powers -> each monster's
    // powers -> relics(acq order) -> stance -> blights -> hand -> discard -> draw
    // cards. Powers are the player+monster stages; the relic/stance/blight/card
    // stages are structural extension points (no S1 power overrides onPlayCard --
    // AbstractPower base only) that light up with relics (B3.24+) and card-level
    // hooks (curses, B3.9). Their call sites live here, in order, empty for now.
    dispatch_actor_powers(s, kActorPlayer, Hook::ON_PLAY_CARD, ctx);
    for (uint8_t m = 0; m < s.monster_count; ++m) {
        dispatch_actor_powers(s, m, Hook::ON_PLAY_CARD, ctx);
    }
    // relics onPlayCard (acquisition order), AFTER player+monster powers (B3.24).
    const RelicView rv = player_relics(s);
    dispatch_relics_on_play_card(s, rv.relics, rv.count, card_id);
    // stance.onPlayCard              -- stanceless skeleton
    // blights onPlayCard             -- none
    // hand / discard / draw cards onPlayCard -- card-level hooks (B3.9)
}

void dispatch_on_use_card(CombatState& s, uint8_t played_pool_index,
                          uint16_t card_id) noexcept {
    HookContext ctx{};
    ctx.card_id = card_id;
    ctx.card_pool_index = played_pool_index;
    // UseCardAction.java:41-64 order -- DISTINCT from onPlayCard: player powers ->
    // player relics -> hand -> discard -> draw cards -> monster powers (monsters
    // LAST). Corruption (native, player power) redirects the played SKILL to
    // exhaust here.
    dispatch_actor_powers(s, kActorPlayer, Hook::ON_USE_CARD, ctx);
    // player relics onUseCard (acquisition order), AFTER player powers and BEFORE
    // monster powers -- the UseCardAction.java:41-64 order (B3.24). Nunchaku/Pen Nib
    // count attacks here.
    const RelicView rv = player_relics(s);
    dispatch_relics_on_use_card(s, rv.relics, rv.count, card_id, played_pool_index);
    // hand / discard / draw cards onUseCard -- card-level hooks (later)
    for (uint8_t m = 0; m < s.monster_count; ++m) {
        dispatch_actor_powers(s, m, Hook::ON_USE_CARD, ctx);
    }
}

// --- Single-source hooks -----------------------------------------------------

void dispatch_on_exhaust(CombatState& s, uint16_t card_id) noexcept {
    HookContext ctx{};
    ctx.card_id = card_id;
    // CardGroup.moveToExhaustPile:851-856 -- relics onExhaust -> player powers
    // onExhaust (list order) -> card.triggerOnExhaust. Feel No Pain + Dark Embrace
    // sequence is decided by the player power-list order here (§5.5).
    // relics onExhaust FIRST (acquisition order), before player powers (B3.24).
    const RelicView rv = player_relics(s);
    dispatch_relics_on_exhaust(s, rv.relics, rv.count, card_id);
    dispatch_actor_powers(s, kActorPlayer, Hook::ON_EXHAUST, ctx);
    // card.triggerOnExhaust (Sentinel-style card-level onExhaust) -- B3.6.
}

void dispatch_on_card_draw(CombatState& s, uint8_t pool_index,
                           uint16_t card_id) noexcept {
    HookContext ctx{};
    ctx.card_id = card_id;
    ctx.card_pool_index = pool_index;
    dispatch_actor_powers(s, kActorPlayer, Hook::ON_CARD_DRAW, ctx);
}

void dispatch_at_end_of_turn_pre_card(CombatState& s) noexcept {
    dispatch_actor_powers(s, kActorPlayer, Hook::AT_END_OF_TURN_PRE_CARD,
                          HookContext{});
}

void dispatch_at_end_of_turn(CombatState& s) noexcept {
    dispatch_actor_powers(s, kActorPlayer, Hook::AT_END_OF_TURN, HookContext{});
}

void dispatch_at_start_of_turn(CombatState& s) noexcept {
    dispatch_actor_powers(s, kActorPlayer, Hook::AT_START_OF_TURN, HookContext{});
}

void dispatch_at_start_of_turn_post_draw(CombatState& s) noexcept {
    dispatch_actor_powers(s, kActorPlayer, Hook::AT_START_OF_TURN_POST_DRAW,
                          HookContext{});
}

void dispatch_on_gained_block(CombatState& s, uint8_t actor,
                              int32_t amount) noexcept {
    if (amount <= 0) {
        return;  // addBlock only fires onGainedBlock when tmp > 0 (:428)
    }
    HookContext ctx{};
    ctx.amount = amount;
    // player relics onPlayerGainedBlock -> the actor's powers onGainedBlock
    // (Juggernaut). Relics fire FIRST (acquisition order), only on the player's own
    // block gain (AbstractCreature.addBlock:426-433; B3.24).
    if (actor == kActorPlayer) {
        const RelicView rv = player_relics(s);
        dispatch_relics_on_gained_block(s, rv.relics, rv.count, amount);
    }
    dispatch_actor_powers(s, actor, Hook::ON_GAINED_BLOCK, ctx);
}

void dispatch_was_hp_lost(CombatState& s, uint8_t victim, uint8_t source,
                          int32_t amount) noexcept {
    if (amount <= 0) {
        return;  // damage:1438 gates the wasHPLost block on damageAmount > 0
    }
    HookContext ctx{};
    ctx.source = source;
    ctx.amount = amount;
    dispatch_actor_powers(s, victim, Hook::WAS_HP_LOST, ctx);
}

// --- APPLY_POWER interception ------------------------------------------------

void dispatch_on_apply_power_source(CombatState& s, uint8_t source,
                                    uint8_t target, uint16_t applied_power_id,
                                    bool applied_is_debuff) noexcept {
    HookContext ctx{};
    ctx.source = source;
    ctx.target = target;
    ctx.applied_power_id = applied_power_id;
    ctx.amount = applied_is_debuff ? 1 : 0;  // relayed for native guards
    // ApplyPowerAction.java:106-109 -- the SOURCE's powers onApplyPower fire
    // FIRST (Sadistic), before the target-side Artifact nullify.
    dispatch_actor_powers(s, source, Hook::ON_APPLY_POWER, ctx);
}

bool apply_power_blocked_by_artifact(CombatState& s, uint8_t target,
                                     bool applied_is_debuff) noexcept {
    if (!applied_is_debuff) {
        return false;  // Artifact only nullifies DEBUFFs (ApplyPowerAction:131)
    }
    PowerSlot* art = find_power(s, target, PowerId::ARTIFACT);
    if (art == nullptr || art->amount <= 0) {
        return false;
    }
    // onSpecificTrigger: decrement one stack; the debuff does NOT land.
    art->amount = static_cast<int16_t>(art->amount - 1);
    // (A 0-stack Artifact slot is left in place; the game removes it, but a
    // 0-amount slot reads as "no charges" for future checks -- amount<=0 above --
    // and the pump has no power-GC pass yet. B3.24 relic/power removal handles GC.)
    return true;
}

// --- Native escape hatch -----------------------------------------------------

void dispatch_native_hook(CombatState& s, Hook hook, PowerId power_id,
                          const HookContext& ctx) noexcept {
    switch (power_id) {
        case PowerId::SADISTIC: {
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
            return;
        }
        case PowerId::RUPTURE: {
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
            return;
        }
        case PowerId::CORRUPTION: {
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
            return;
        }
        case PowerId::ARTIFACT:
            // Artifact's body is the target-side nullify in
            // apply_power_blocked_by_artifact (consumed inside APPLY_POWER); it
            // has no source-side / per-power-list hook body.
            return;
        default:
            return;  // an unrecognized native power is a safe no-op
    }
}

}  // namespace sts::engine
