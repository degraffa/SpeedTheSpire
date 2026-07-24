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
    if (step.op == static_cast<decltype(step.op)>(Opcode::BLOCK)) {  // registry mirror
        // A power's block is a direct GainBlockAction (no card applyPowers), so it
        // does NOT get Dexterity -- flag it so op_block skips the modifyBlock pass.
        item.flags |= kBlockNoPowers;
    }
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
        case PowerId::LOSE_STRENGTH: {
            // LoseStrengthPower.atEndOfTurn (LoseStrengthPower.java:40-44): at end
            // of turn, addToBot ApplyPower(Strength, -amount) then
            // RemoveSpecificPower(self) -- the "temporary Strength" reversal (Flex).
            // Both queued (addToBot), so the Strength reduction and the self-removal
            // resolve on later pump iterations, NOT during this power-list walk.
            if (hook != Hook::AT_END_OF_TURN) {
                return;
            }
            ActionQueueItem down{};
            down.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
            down.src = ctx.owner;
            down.tgt = ctx.owner;
            down.amount = -ctx.power_amount;  // Strength -amount
            down.flags = make_apply_power_flags(PowerId::STRENGTH);
            add_to_bottom(s, down);
            ActionQueueItem rem{};
            rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
            rem.src = ctx.owner;
            rem.tgt = ctx.owner;
            rem.flags = make_apply_power_flags(PowerId::LOSE_STRENGTH);
            add_to_bottom(s, rem);
            return;
        }
        case PowerId::THORNS: {
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
            return;
        }
        case PowerId::PLATED_ARMOR: {
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
            return;
        }
        case PowerId::REGEN: {
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
            return;
        }
        case PowerId::LOSE_DEXTERITY: {
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
            return;
        }
        case PowerId::RITUAL: {
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
            return;
        }
        case PowerId::FRAIL: {
            // FrailPower.atEndOfRound (FrailPower.java:42-54): a newly-created,
            // monster-sourced player instance consumes justApplied without losing
            // duration. Later rounds reduce one stack and remove at zero. This
            // dispatcher runs after monster turns and before next-turn setup.
            if (hook != Hook::AT_END_OF_ROUND || ctx.owner != kActorPlayer) {
                return;
            }
            PowerSlot* fp = find_power(s, kActorPlayer, PowerId::FRAIL);
            if (fp == nullptr) {
                s.flags &= ~kCombatFlagFrailJustApplied;
                return;
            }
            if ((s.flags & kCombatFlagFrailJustApplied) != 0u) {
                s.flags &= ~kCombatFlagFrailJustApplied;
                return;
            }
            ActionQueueItem reduce{};
            reduce.opcode = static_cast<uint16_t>(Opcode::REDUCE_POWER);
            reduce.src = ctx.owner;
            reduce.tgt = ctx.owner;
            reduce.amount = 1;
            reduce.flags = make_apply_power_flags(PowerId::FRAIL);
            add_to_bottom(s, reduce);  // ReducePowerAction, FrailPower.java:52
            return;
        }
        case PowerId::CURL_UP: {
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
            return;
        }
        case PowerId::NEXT_TURN_BLOCK: {
            // NextTurnBlockPower.atStartOfTurn (NextTurnBlockPower.java:44-50):
            // addToBot GainBlockAction(owner, amount) then addToBot
            // RemoveSpecificPowerAction(self). A direct GainBlockAction -> no
            // Dexterity (kBlockNoPowers); both queued, so the block lands AFTER
            // start_of_turn's synchronous block decay -- exactly the game's
            // "gain Block at the start of your next turn" (Self-Forming Clay).
            if (hook != Hook::AT_START_OF_TURN) {
                return;
            }
            ActionQueueItem blk{};
            blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
            blk.src = ctx.owner;
            blk.tgt = ctx.owner;
            blk.amount = ctx.power_amount;
            blk.flags = kBlockNoPowers;
            add_to_bottom(s, blk);
            ActionQueueItem rem{};
            rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
            rem.src = ctx.owner;
            rem.tgt = ctx.owner;
            rem.flags = make_apply_power_flags(PowerId::NEXT_TURN_BLOCK);
            add_to_bottom(s, rem);
            return;
        }
        case PowerId::RAGE: {
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
            return;
        }
        case PowerId::FLAME_BARRIER: {
            // FlameBarrierPower (B3.6). onAttacked (FlameBarrierPower.java:
            // 53-59): reflect `amount` THORNS damage to a DISTINCT attacker --
            // op_damage already gated dispatch to NORMAL src != tgt after
            // decrementBlock (fires whether or not the hit penetrated); the
            // owner != attacker guard is re-checked. addToTop, THORNS-typed
            // (skips the NORMAL-only power modifiers -- a Vulnerable attacker
            // is NOT amplified). atStartOfTurn (:62-64): addToBot
            // RemoveSpecificPowerAction -- gone at the player's next turn start.
            if (hook == Hook::ON_ATTACKED) {
                if (ctx.source == ctx.owner) {
                    return;
                }
                ActionQueueItem dmg{};
                dmg.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
                dmg.src = ctx.owner;
                dmg.tgt = ctx.source;
                dmg.amount = ctx.power_amount;
                dmg.flags = make_damage_flags(DamageType::THORNS);
                add_to_top(s, dmg);  // addToTop (FlameBarrierPower.java:56)
                return;
            }
            if (hook == Hook::AT_START_OF_TURN) {
                ActionQueueItem rem{};
                rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
                rem.src = ctx.owner;
                rem.tgt = ctx.owner;
                rem.flags = make_apply_power_flags(PowerId::FLAME_BARRIER);
                add_to_bottom(s, rem);  // addToBot (FlameBarrierPower.java:63)
                return;
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
