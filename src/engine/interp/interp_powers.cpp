// POWER-domain opcode bodies -- the power-slot list writers and SPOT_WEAKNESS
// (moved verbatim out of interp.cpp's anonymous namespace; see interp_ops.hpp
// for the split's rationale).

#include "interp_powers.hpp"

#include <cstdint>

#include "interp_ops.hpp"                   // actor_powers
#include "sts/engine/action_queue.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // MonsterIntent (SPOT_WEAKNESS intent gate)
#include "sts/engine/power_hooks.hpp"       // power hook dispatch (onApplyPower)
#include "sts/engine/powers.hpp"            // power_def / PowerType (APPLY_POWER interception)
#include "sts/engine/relic_hooks.hpp"       // player_has_relic (Champion Belt/Ginger/Turnip)
#include "sts/engine/types.hpp"

namespace sts::engine {

namespace {

// Non-consuming "does `actor` carry `id` with a live stack?". Deliberately
// distinct from apply_power_blocked_by_artifact, which SPENDS an Artifact stack:
// Champion Belt's gate only ASKS the question (ApplyPowerAction.java:111), and
// answering it must not consume anything.
[[nodiscard]] bool actor_carries_power(const CombatState& s, uint8_t actor,
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

// Presence, NOT magnitude: does `actor` have a slot for `id` at all? Barricade
// and Corruption both live with amount -1 (BarricadePower.java:22 /
// CorruptionPower.java:27), so the amount-gated actor_carries_power above would
// answer "no" for either of them.
[[nodiscard]] bool actor_has_power_slot(const CombatState& s, uint8_t actor,
                                        PowerId id) noexcept {
    const PowerView pv = actor_powers(s, actor);
    for (uint8_t i = 0; i < pv.count; ++i) {
        if (pv.slots[i].power_id == static_cast<uint16_t>(id)) {
            return true;
        }
    }
    return false;
}

// The "the player already has this, do nothing" guard two RARE POWER cards spell
// out in their own use(): Barricade.use (Barricade.java:32-40) and
// Corruption.use (Corruption.java:43-51) both scan p.powers for their own ID and
// queue the ApplyPowerAction only when it is absent. A registry effect program
// has no conditional, so the guard lives here instead, at the apply.
//
// Moving it from use()-time to apply-time is observably identical: the two side
// effects a skipped ApplyPowerAction would also have skipped -- the source
// onApplyPower fan-out (Sadistic) and the target Artifact nullify -- are both
// gated on the applied power being a DEBUFF (SadisticPower.java:39,
// ApplyPowerAction.java:131), and both of these are BUFFs.
[[nodiscard]] bool apply_is_a_no_op_repeat(const CombatState& s, uint8_t tgt,
                                           PowerId id) noexcept {
    if (id != PowerId::BARRICADE && id != PowerId::CORRUPTION) {
        return false;
    }
    return actor_has_power_slot(s, tgt, id);
}

}  // namespace

// APPLY_POWER: stack PowerId(flags) x amount onto tgt. Stacks onto an existing
// slot of the same id, else appends a new slot (hard cap kPowerCap -- overflow
// is a silent no-op here rather than an assert, since a malformed item must not
// crash; real card play never overflows 24 skeleton powers).
//
// Interception (ApplyPowerAction.java:106-138): (1) the SOURCE's powers'
// onApplyPower fire FIRST (Sadistic queues damage on a debuffed target); (2) if
// the TARGET has Artifact and the applied power is a DEBUFF, one Artifact stack is
// consumed and the power does NOT land. Both are no-ops without Sadistic/Artifact,
// so skeleton APPLY_POWER (Bash's Vulnerable, Bellow's Strength) is unchanged.
void op_apply_power(CombatState& s, uint8_t src, uint8_t tgt, PowerId id,
                    int amount) noexcept {
    if (id == PowerId::NONE) {
        return;
    }
    // ApplyPowerAction.update:102-105: applying No Draw to a target that
    // ALREADY has No Draw is a whole-action no-op -- it short-circuits BEFORE the
    // source onApplyPower hooks and the Artifact nullify, and never stacks.
    if (id == PowerId::NO_DRAW) {
        const PowerView pv = actor_powers(s, tgt);
        for (uint8_t i = 0; i < pv.count; ++i) {
            if (pv.slots[i].power_id == static_cast<uint16_t>(PowerId::NO_DRAW)) {
                return;
            }
        }
    }
    // Barricade / Corruption: the CARD refuses to queue a second application at
    // all (see apply_is_a_no_op_repeat), so a repeat lands nowhere and stacks
    // nothing -- their slot amount stays the -1 marker their ctors set.
    if (apply_is_a_no_op_repeat(s, tgt, id)) {
        return;
    }
    const PowerDef* applied_def = power_def(id);
    // The applied instance's PowerType. Strength/Dexterity flip to DEBUFF when
    // constructed with a non-positive amount (StrengthPower ctor :37 calls
    // updateDescription :81-89, `amount > 0 ? BUFF : DEBUFF`; DexterityPower
    // likewise :74-82) -- so Disarm's Strength(-N) IS Artifact-nullified and
    // Sadistic-visible, while Spot Weakness's Strength(+N) stays a BUFF.
    const bool negative_stat_flip =
        (id == PowerId::STRENGTH || id == PowerId::DEXTERITY) && amount <= 0;
    const bool is_debuff =
        (applied_def != nullptr && applied_def->type == PowerType::DEBUFF) ||
        negative_stat_flip;
    // (1) source-side onApplyPower (fires before the power lands).
    dispatch_on_apply_power_source(s, src, tgt, static_cast<uint16_t>(id),
                                   is_debuff);
    // (2) Champion Belt (ApplyPowerAction.java:111-113 -> ChampionsBelt.onTrigger,
    // ChampionsBelt.java:172-176): the player owns it, the source IS the player,
    // target != source, the applied power is Vulnerable, and the target does NOT
    // already have Artifact -> addToBot ApplyPowerAction(target, player, Weak 1).
    // The Artifact test is READ-ONLY and happens HERE, before step (4) spends the
    // stack, so a Vulnerable that Artifact is about to eat grants no Weak.
    if (id == PowerId::VULNERABLE && src == kActorPlayer && tgt != src &&
        player_has_relic(s, RelicId::CHAMPION_BELT) &&
        !actor_carries_power(s, tgt, PowerId::ARTIFACT)) {
        ActionQueueItem weak{};
        weak.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        weak.src = kActorPlayer;
        weak.tgt = tgt;
        weak.amount = 1;
        weak.flags = make_apply_power_flags(PowerId::WEAK);
        add_to_bottom(s, weak);
    }
    // (3) Ginger / Turnip (ApplyPowerAction.java:119-124 and :125-130): each
    // returns from update() WITHOUT applying when the player owns the relic and
    // the PLAYER is the target of Weakened / Frail respectively. Both sit after
    // the source hooks and BEFORE the Artifact nullify, so the rejected debuff
    // does not spend an Artifact stack.
    if (tgt == kActorPlayer &&
        ((id == PowerId::WEAK && player_has_relic(s, RelicId::GINGER)) ||
         (id == PowerId::FRAIL && player_has_relic(s, RelicId::TURNIP)))) {
        return;
    }
    // (4) target-side Artifact nullify: a consumed Artifact stack blocks the debuff.
    if (apply_power_blocked_by_artifact(s, tgt, is_debuff)) {
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
            if (id == PowerId::COMBUST && tgt == kActorPlayer) {
                uint32_t hp_loss =
                    (s.flags & kCombatFlagCombustHpLossMask) >> kCombatFlagCombustHpLossShift;
                if (hp_loss < 0xFFu) {
                    ++hp_loss;
                }
                s.flags = (s.flags & ~kCombatFlagCombustHpLossMask) |
                          (hp_loss << kCombatFlagCombustHpLossShift);
            }
            slots[i].amount = static_cast<int16_t>(slots[i].amount + amount);
            // StrengthPower/DexterityPower.stackPower (:48-53 / :44-49): a stack
            // landing on EXACTLY 0 queues the slot's removal (addToTop
            // RemoveSpecificPowerAction) -- Disarm cancelling equal Strength, or
            // Flex's end-of-turn reversal. Queued, not synchronous: the 0-amount
            // slot remains visible until the queued removal resolves.
            if ((id == PowerId::STRENGTH || id == PowerId::DEXTERITY) &&
                slots[i].amount == 0) {
                ActionQueueItem rem{};
                rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
                rem.src = tgt;
                rem.tgt = tgt;
                rem.flags = make_apply_power_flags(id);
                add_to_top(s, rem);
            }
            return;
        }
    }
    if (*count >= kPowerCap) {
        return;
    }
    if (id == PowerId::CURL_UP && tgt < kMonsterCap) {
        // A newly-created CurlUpPower starts with triggered=false
        // (CurlUpPower.java:25,27-34). Existing instances preserve the latch when
        // stacked; only the new-slot path clears it.
        s.monsters[tgt].flags = static_cast<uint16_t>(
            s.monsters[tgt].flags & ~kMonsterFlagCurlUpTriggered);
    }
    if (id == PowerId::FRAIL && tgt == kActorPlayer) {
        // All in-scope player Frail constructors pass isSourceMonster=true
        // (Shame and Act-1 monsters). Only a NEW instance gets justApplied;
        // stacking returned above and therefore preserves the existing latch.
        s.flags |= kCombatFlagFrailJustApplied;
    }
    if (id == PowerId::COMBUST && tgt == kActorPlayer) {
        s.flags = (s.flags & ~kCombatFlagCombustHpLossMask) |
                  (1u << kCombatFlagCombustHpLossShift);
    }
    slots[*count].power_id = pid;
    slots[*count].amount = static_cast<int16_t>(amount);
    ++*count;
}

// REMOVE_POWER (RemoveSpecificPowerAction): drop PowerId(flags) from tgt's power
// list (shifting the tail down). No-op if the actor lacks the power.
void op_remove_power(CombatState& s, uint8_t tgt, PowerId id) noexcept {
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
            for (uint8_t j = static_cast<uint8_t>(i + 1); j < *count; ++j) {
                slots[j - 1] = slots[j];
            }
            --*count;
            slots[*count] = PowerSlot{};  // zero the vacated tail slot
            if (id == PowerId::CURL_UP && tgt < kMonsterCap) {
                // RemoveSpecificPowerAction destroys the CurlUpPower instance;
                // its private triggered latch leaves with it.
                s.monsters[tgt].flags = static_cast<uint16_t>(
                    s.monsters[tgt].flags & ~kMonsterFlagCurlUpTriggered);
            }
            if (id == PowerId::FRAIL && tgt == kActorPlayer) {
                s.flags &= ~kCombatFlagFrailJustApplied;
            }
            if (id == PowerId::COMBUST && tgt == kActorPlayer) {
                s.flags &= ~kCombatFlagCombustHpLossMask;
            }
            return;
        }
    }
}

// REDUCE_POWER (ReducePowerAction): subtract `amount` from one power and remove
// the slot when it reaches zero. Kept as a queued opcode so an atEndOfRound power
// cannot mutate/compact the list while the dispatcher is still iterating it.
void op_reduce_power(CombatState& s, uint8_t tgt, PowerId id,
                     int amount) noexcept {
    if (amount <= 0) {
        return;
    }
    PowerSlot* slots = nullptr;
    uint8_t count = 0;
    if (tgt == kActorPlayer) {
        slots = s.player_powers;
        count = s.player_power_count;
    } else if (tgt < kMonsterCap) {
        slots = s.monsters[tgt].powers;
        count = s.monsters[tgt].power_count;
    } else {
        return;
    }
    const uint16_t pid = static_cast<uint16_t>(id);
    for (uint8_t i = 0; i < count; ++i) {
        if (slots[i].power_id != pid) {
            continue;
        }
        slots[i].amount = static_cast<int16_t>(slots[i].amount - amount);
        if (slots[i].amount <= 0) {
            op_remove_power(s, tgt, id);
        }
        return;
    }
}

// DOUBLE_STRENGTH (LimitBreakAction.update, LimitBreakAction.java:26-33): when
// the player HAS a Strength power, addToTop ApplyPowerAction(player, player,
// StrengthPower(player, strAmt), strAmt) with strAmt read from the slot at
// execute time -- a doubling. `hasPower` is a bare presence test, so a NEGATIVE
// Strength is doubled too (Limit Break after a Disarm deepens the penalty), and a
// slot sitting at exactly 0 while its queued removal is still pending applies 0.
// addToTop, not addToBot: the doubled Strength jumps the queue.
void op_double_strength(CombatState& s) noexcept {
    const PowerView pv = actor_powers(s, kActorPlayer);
    for (uint8_t i = 0; i < pv.count; ++i) {
        if (pv.slots[i].power_id != static_cast<uint16_t>(PowerId::STRENGTH)) {
            continue;
        }
        ActionQueueItem up{};
        up.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        up.src = kActorPlayer;
        up.tgt = kActorPlayer;
        up.amount = pv.slots[i].amount;
        up.flags = make_apply_power_flags(PowerId::STRENGTH);
        add_to_top(s, up);
        return;
    }
}

// SPOT_WEAKNESS (SpotWeaknessAction.update:32-40): if the target monster's
// telegraphed move deals attack damage (getIntentBaseDmg() >= 0 -- setMove
// stores baseDamage -1 for every non-attack move, AbstractMonster.java:451-463,
// and createIntent copies it, :412), addToBot ApplyPowerAction(player,
// StrengthPower(player, amount), amount). MonsterState.intent stores the
// MonsterIntent classification the monster module decided (monster modules set
// it in their set_move); the ATTACK* variants are exactly the moves constructed
// with a non-negative baseDamage. No liveness check: the Java action only
// null-checks the target.
void op_spot_weakness(CombatState& s, uint8_t tgt, int amount) noexcept {
    if (tgt >= kMonsterCap) {
        return;
    }
    const MonsterIntent intent =
        static_cast<MonsterIntent>(s.monsters[tgt].intent);
    // Every AbstractMonster.Intent variant a monster can be given a non-negative
    // baseDamage with -- the ATTACK* family (AbstractMonster.java:451-463: the
    // damage-carrying setMove overloads store baseDamage, the others leave it
    // at -1). ATTACK_BUFF is the fourth member: The Guardian telegraphs Twin
    // Slam with it (TheGuardian.java:204), and while this predicate omitted it
    // Spot Weakness silently paid out nothing against that telegraphed attack.
    const bool attacks = intent == MonsterIntent::ATTACK ||
                         intent == MonsterIntent::ATTACK_DEFEND ||
                         intent == MonsterIntent::ATTACK_DEBUFF ||
                         intent == MonsterIntent::ATTACK_BUFF;
    if (!attacks) {
        return;
    }
    ActionQueueItem up{};
    up.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    up.src = kActorPlayer;
    up.tgt = kActorPlayer;
    up.amount = amount;
    up.flags = make_apply_power_flags(PowerId::STRENGTH);
    add_to_bottom(s, up);  // addToBot (SpotWeaknessAction.java:35)
}

}  // namespace sts::engine
