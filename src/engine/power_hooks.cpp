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
//
// The native bodies themselves live in per-batch translation units under
// src/engine/powers/ (grouped by the registry/powers.yaml batch that introduced
// them); this file keeps the framework plus the power_native_fn dispatch table,
// mirroring monster_dispatch.cpp's per-monster TUs + function-pointer switch.

#include "sts/engine/power_hooks.hpp"

#include <cstdint>

#include "powers/power_native.hpp"          // PowerNativeFn, actor_power_list, find_power
#include "powers/powers_b3_2.hpp"           // B3.2 framework powers
#include "powers/powers_b3_25.hpp"          // B3.25 relic-applied power
#include "powers/powers_b3_4.hpp"           // B3.4 (Flex)
#include "powers/powers_b3_6.hpp"           // B3.6 red-uncommon-skill powers
#include "powers/powers_b3_7.hpp"           // B3.7 red-uncommon-power cards
#include "powers/powers_b3_9.hpp"           // B3.9 curse-applied debuff
#include "powers/powers_potion_support.hpp" // potion-support-powers follow-up
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
    if (step.op == static_cast<decltype(step.op)>(Opcode::BLOCK)) {  // registry mirror
        // A power's block is a direct GainBlockAction (no card applyPowers), so it
        // does NOT get Dexterity -- flag it so op_block skips the modifyBlock pass.
        item.flags |= kBlockNoPowers;
    }
    add_to_bottom(s, item);
}

// (PowerListView / actor_power_list / find_power now live in
// powers/power_native.hpp -- the native bodies in src/engine/powers/ need them
// too, so they are shared inline helpers rather than file-local statics.)

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

void dispatch_on_exhaust(CombatState& s, uint8_t pool_index,
                         uint16_t card_id) noexcept {
    HookContext ctx{};
    ctx.card_id = card_id;
    ctx.card_pool_index = pool_index;
    // CardGroup.moveToExhaustPile:851-857 -- relics onExhaust -> player powers
    // onExhaust (list order) -> card.triggerOnExhaust. Feel No Pain + Dark Embrace
    // sequence is decided by the player power-list order here (§5.5).
    // relics onExhaust FIRST (acquisition order), before player powers (B3.24).
    const RelicView rv = player_relics(s);
    dispatch_relics_on_exhaust(s, rv.relics, rv.count, card_id);
    dispatch_actor_powers(s, kActorPlayer, Hook::ON_EXHAUST, ctx);
    // card.triggerOnExhaust (B3.6): the exhausted card's own on_exhaust program,
    // LAST in the §5.5 order. Sentinel addToTop's its GainEnergyAction
    // (Sentinel.java:37-43) -- steps are queued add_to_top, in REVERSE program
    // order so a multi-step program still resolves first-step-first (every S1
    // program is single-step).
    if (pool_index < kCardPoolCap) {
        const CardDef* def = card_def(static_cast<CardId>(card_id));
        if (def != nullptr) {
            const CardEffectView ox =
                card_on_exhaust_steps(*def, s.card_pool[pool_index].upgrade);
            for (uint8_t k = ox.count; k > 0; --k) {
                const CardEffectStep& step = ox.steps[k - 1];
                ActionQueueItem item{};
                item.opcode = static_cast<uint16_t>(step.op);
                item.src = kActorPlayer;
                item.tgt = kActorPlayer;  // in-scope on-exhaust steps are SELF
                item.amount = step.amount;
                item.flags = step.extra;
                add_to_top(s, item);
            }
        }
    }
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

void dispatch_at_end_of_round(CombatState& s) noexcept {
    // MonsterGroup.applyEndOfTurnPowers (MonsterGroup.java:290-304), in order:
    //   (1) each LIVE monster: applyEndOfTurnTriggers -> monster powers'
    //       atEndOfTurnPreEndTurnCards(false) + atEndOfTurn(false). No B3.13
    //       monster power binds these (Metallicize is B3.19; the Cultist's RITUAL
    //       guards atEndOfTurn on isPlayer -> no-op for a monster owner). Call
    //       sites kept for faithful ordering / future powers.
    //   (2) player powers atEndOfRound (a player-owner Ritual is onPlayer -> its
    //       atEndOfRound is a no-op; guarded in the native body).
    //   (3) each LIVE monster: its powers atEndOfRound -- the Cultist Ritual
    //       Strength ramp fires here.
    // "live" == hp > 0 (dying/escaping are skipped in the game; we model dying as
    // hp <= 0). No-op unless a power binds these hooks -> jaw-worm fixtures unchanged.
    for (uint8_t m = 0; m < s.monster_count; ++m) {
        if (s.monsters[m].hp <= 0) {
            continue;
        }
        dispatch_actor_powers(s, m, Hook::AT_END_OF_TURN_PRE_CARD, HookContext{});
        dispatch_actor_powers(s, m, Hook::AT_END_OF_TURN, HookContext{});
    }
    dispatch_actor_powers(s, kActorPlayer, Hook::AT_END_OF_ROUND, HookContext{});
    for (uint8_t m = 0; m < s.monster_count; ++m) {
        if (s.monsters[m].hp <= 0) {
            continue;
        }
        dispatch_actor_powers(s, m, Hook::AT_END_OF_ROUND, HookContext{});
    }
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

void dispatch_on_attacked(CombatState& s, uint8_t victim, uint8_t attacker,
                          int32_t amount) noexcept {
    HookContext ctx{};
    ctx.source = attacker;
    ctx.amount = amount;
    // The victim's powers onAttacked (Thorns). op_damage has already gated this to
    // NORMAL damage from a distinct attacker, so no THORNS/HP_LOSS re-entry.
    dispatch_actor_powers(s, victim, Hook::ON_ATTACKED, ctx);
}

void dispatch_was_hp_lost(CombatState& s, uint8_t victim, uint8_t source,
                          int32_t amount, uint8_t damage_type) noexcept {
    if (amount <= 0) {
        return;  // damage:1438 gates the wasHPLost block on damageAmount > 0
    }
    HookContext ctx{};
    ctx.source = source;
    ctx.amount = amount;
    ctx.damage_type = damage_type;
    dispatch_actor_powers(s, victim, Hook::WAS_HP_LOST, ctx);
    // AbstractPlayer.damage:1445-1449 -- powers' wasHPLost first, THEN the
    // player's relics' wasHPLost (acquisition order; no source/type guard at the
    // relic loop -- per-relic guards live in the bodies). Player victim only
    // (monsters have no relics). No-op with an empty mirror, so the 20 combat
    // fixtures are unchanged (B3.25 wires the site; B3.24's Centennial Puzzle /
    // Red Skull and B3.25's Self-Forming Clay become live through it).
    if (victim == kActorPlayer) {
        const RelicView rv = player_relics(s);
        dispatch_relics_was_hp_lost(s, rv.relics, rv.count, amount);
    }
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
//
// The dispatch table: PowerId -> the native body's function pointer, or nullptr
// for a power with no per-power-list body. Structure mirrors
// monster_dispatch.cpp's monster_init_fn (a plain switch, data-oriented, no
// virtual dispatch); each power batch adds its cases here and a translation unit
// under src/engine/powers/.

PowerNativeFn power_native_fn(PowerId id) noexcept {
    switch (id) {
        // B3.2 framework powers (powers/powers_b3_2.cpp)
        case PowerId::SADISTIC:
            return &power_native_sadistic;
        case PowerId::DARK_EMBRACE:
            return &power_native_dark_embrace;
        case PowerId::COMBUST:
            return &power_native_combust;
        case PowerId::RUPTURE:
            return &power_native_rupture;
        case PowerId::CORRUPTION:
            return &power_native_corruption;
        case PowerId::RAGE:
            return &power_native_rage;
        // B3.4 (Flex) (powers/powers_b3_4.cpp)
        case PowerId::LOSE_STRENGTH:
            return &power_native_lose_strength;
        // potion-support-powers follow-up (powers/powers_potion_support.cpp)
        case PowerId::THORNS:
            return &power_native_thorns;
        case PowerId::PLATED_ARMOR:
            return &power_native_plated_armor;
        case PowerId::REGEN:
            return &power_native_regen;
        case PowerId::LOSE_DEXTERITY:
            return &power_native_lose_dexterity;
        case PowerId::RITUAL:
            return &power_native_ritual;
        case PowerId::CURL_UP:
            return &power_native_curl_up;
        // B3.9 curse-applied debuff (powers/powers_b3_9.cpp)
        case PowerId::FRAIL:
            return &power_native_frail;
        // B3.25 relic-applied power (powers/powers_b3_25.cpp)
        case PowerId::NEXT_TURN_BLOCK:
            return &power_native_next_turn_block;
        // B3.6 red-uncommon-skill powers (powers/powers_b3_6.cpp)
        case PowerId::FLAME_BARRIER:
            return &power_native_flame_barrier;
        // B3.7 red-uncommon-power cards (powers/powers_b3_7.cpp)
        case PowerId::EVOLVE:
            return &power_native_evolve;
        case PowerId::FIRE_BREATHING:
            return &power_native_fire_breathing;
        case PowerId::ARTIFACT:
            // Artifact's body is the target-side nullify in
            // apply_power_blocked_by_artifact (consumed inside APPLY_POWER); it
            // has no source-side / per-power-list hook body.
            return nullptr;
        default:
            return nullptr;  // an unrecognized native power is a safe no-op
    }
}

void dispatch_native_hook(CombatState& s, Hook hook, PowerId power_id,
                          const HookContext& ctx) noexcept {
    const PowerNativeFn fn = power_native_fn(power_id);
    if (fn != nullptr) {
        fn(s, hook, ctx);
    }
}

}  // namespace sts::engine
