// POWER-domain opcode bodies -- the power-slot list writers and SPOT_WEAKNESS
// (moved verbatim out of interp.cpp's anonymous namespace; see interp_ops.hpp
// for the split's rationale).

#include "interp_powers.hpp"

#include <cstdint>

#include "interp_ops.hpp"                   // actor_powers
#include "../powers/power_duration_debuff.hpp"  // duration_debuff_starts_just_applied
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
// Barricade's guard can move from use-time to apply-time without changing its
// two other possible effects: source onApplyPower (Sadistic) and target
// Artifact nullify are both DEBUFF-gated (SadisticPower.java:39,
// ApplyPowerAction.java:131), while Barricade is a BUFF. Corruption additionally
// has a use-time guard in card_play.cpp because constructing its action performs
// the synchronous four-pile cost walk; this apply-time check remains necessary
// if two applications were queued before the first one landed.
[[nodiscard]] bool apply_is_a_no_op_repeat(const CombatState& s, uint8_t tgt,
                                           PowerId id) noexcept {
    if (id != PowerId::BARRICADE && id != PowerId::CORRUPTION) {
        return false;
    }
    return actor_has_power_slot(s, tgt, id);
}

// Resolve a power-slot list for `tgt`. Returns false (and leaves the outputs
// untouched) for an actor lane that owns no list.
[[nodiscard]] bool power_list_for(CombatState& s, uint8_t tgt, PowerSlot*& slots,
                                  uint8_t*& count) noexcept {
    if (tgt == kActorPlayer) {
        slots = s.player_powers;
        count = &s.player_power_count;
        return true;
    }
    if (tgt < kMonsterCap) {
        slots = s.monsters[tgt].powers;
        count = &s.monsters[tgt].power_count;
        return true;
    }
    return false;
}

// The slot REMOVE_POWER / REDUCE_POWER act on: the first slot carrying `pid`,
// or -- when the item carries an instance key (interp.hpp
// power_instance_key_present) -- the first slot whose {amount, counter} also
// match. Returns `count` when nothing matches.
[[nodiscard]] uint8_t find_target_slot(const PowerSlot* slots, uint8_t count,
                                       uint16_t pid, uint32_t flags) noexcept {
    const bool keyed = power_instance_key_present(flags);
    const int want_amount = power_instance_amount(flags);
    const int want_counter = power_instance_counter(flags);
    for (uint8_t i = 0; i < count; ++i) {
        if (slots[i].power_id != pid) {
            continue;
        }
        if (keyed && (slots[i].amount != want_amount ||
                      slots[i].counter != want_counter)) {
            continue;
        }
        return i;
    }
    return count;
}

// Drop slot `i` from a list, shifting the tail down and zeroing the vacated
// row. Carries the three per-power side effects a destroyed instance has.
//
// THE REMOVAL CHOKE POINT. Every destruction in the engine reaches here --
// op_remove_power (REMOVE_POWER, itself what REMOVE_DEBUFFS expands into) and
// op_reduce_power's fall-to-zero -- which is what makes it the right place to
// fire the power's own `onRemove`. The Java has no single such point; it has two
// (AbstractCreature.removePower and RemoveSpecificPowerAction.update,
// RemoveSpecificPowerAction.java:29-40), and BOTH call p.onRemove() before the
// list drops the object. A native power body that decrements its own slot and
// zeroes power_id in place therefore BYPASSES its own onRemove -- see the note in
// power_plated_armor.cpp, which is exactly the bug that made this a choke point
// rather than a coincidence.
void remove_slot_at(CombatState& s, uint8_t tgt, PowerSlot* slots,
                    uint8_t* count, uint8_t i) noexcept {
    const PowerId id = static_cast<PowerId>(slots[i].power_id);
    // p.onRemove() BEFORE the list drops it: the body can still read its own
    // {amount, counter}, and it must not mutate the list (we are mid-compaction),
    // so a body that needs another removal queues a REMOVE_POWER item.
    dispatch_on_power_removed(s, tgt, slots[i], i);
    for (uint8_t j = static_cast<uint8_t>(i + 1); j < *count; ++j) {
        slots[j - 1] = slots[j];
    }
    --*count;
    slots[*count] = PowerSlot{};  // zero the vacated tail slot
    if (id == PowerId::CURL_UP && tgt < kMonsterCap) {
        // RemoveSpecificPowerAction destroys the CurlUpPower instance;
        // its private triggered latch leaves with it.
        s.monsters[tgt].flags &= ~kMonsterFlagCurlUpTriggered;
    }
    // Frail's justApplied latch used to be cleared here, out of a
    // CombatState.flags bit. It now lives in the slot's own `counter`
    // (power_duration_debuff.hpp), which this function has already zeroed with
    // the rest of the row -- so removal needs no special case for Frail, Weak or
    // Vulnerable.
    if (id == PowerId::COMBUST && tgt == kActorPlayer) {
        s.flags &= ~kCombatFlagCombustHpLossMask;
    }
}

// The sort key of ApplyPowerAction.java:167's Collections.sort:
// AbstractPower.compareTo (AbstractPower.java:366-368) is
// `this.priority - other.priority`, the field defaulting to 5
// (AbstractPower.java:66). The registry mirrors every ctor override
// (powers.yaml `priority:`); an id with no row takes the default.
[[nodiscard]] uint8_t power_sort_priority(uint16_t pid) noexcept {
    const PowerDef* def = power_def(static_cast<PowerId>(pid));
    return def != nullptr ? def->priority : sts::registry::kDefaultPowerPriority;
}

// ApplyPowerAction.java:167: `Collections.sort(this.target.powers)` runs after
// every NEW power is appended (the !hasBuffAlready branch ONLY -- the stacking
// branch :140-161 never re-sorts, and the synchronous AbstractCreature.addPower
// :506-527 never sorts at all). Collections.sort is a stable merge sort, so the
// live list is priority-major with insertion order preserved inside a priority
// class. Reproduced as an in-place insertion sort (stable; the list is at most
// kPowerCap = 24 slots) over WHOLE PowerSlot rows, so an instanced power's
// per-slot amount/counter travel with their slot. Safe against queued
// REMOVE/REDUCE items: instance keys match {amount, counter} values, never a
// slot index (interp.hpp power_instance_key_present).
void sort_powers_like_the_game(PowerSlot* slots, uint8_t count) noexcept {
    for (uint8_t i = 1; i < count; ++i) {
        const PowerSlot key = slots[i];
        const uint8_t kp = power_sort_priority(key.power_id);
        int j = static_cast<int>(i) - 1;
        while (j >= 0 && power_sort_priority(slots[j].power_id) > kp) {
            slots[j + 1] = slots[j];
            --j;
        }
        slots[j + 1] = key;
    }
}

// The stack-or-append list write shared by op_apply_power (the ApplyPowerAction
// shape) and add_power_direct (the bare AbstractCreature.addPower shape).
// Defined below op_apply_power, whose body it used to be.
void add_power_to_list(CombatState& s, uint8_t tgt, PowerId id, int amount,
                       int counter, bool is_source_monster,
                       const PowerDef* applied_def,
                       bool sort_after_append) noexcept;

}  // namespace

// APPLY_POWER: stack PowerId(flags) x amount onto tgt. Stacks onto an existing
// slot of the same id, else appends a new slot (hard cap kPowerCap -- overflow
// is a silent no-op here rather than an assert, since a malformed item must not
// crash; real card play never overflows 24 skeleton powers). A power the
// registry marks `instanced` skips the merge entirely and ALWAYS appends -- see
// that branch below for the Java shape it models.
//
// `counter` is the applied instance's SECOND number (PowerSlot.counter,
// types.hpp), carried in the item's flags bits 16..31. It is 0 for every power
// that declares no meaning for it, so the whole feature is inert for the
// pre-existing set.
//
// Interception (ApplyPowerAction.java:106-138): (1) the SOURCE's powers'
// onApplyPower fire FIRST (Sadistic queues damage on a debuffed target); (2) if
// the TARGET has Artifact and the applied power is a DEBUFF, one Artifact stack is
// consumed and the power does NOT land. Both are no-ops without Sadistic/Artifact,
// so skeleton APPLY_POWER (Bash's Vulnerable, Bellow's Strength) is unchanged.
void op_apply_power(CombatState& s, uint8_t src, uint8_t tgt, PowerId id,
                    int amount, int counter,
                    bool is_source_monster) noexcept {
    if (id == PowerId::NONE) {
        return;
    }
    // ApplyPowerAction.update:97-100 -- THE VERY FIRST THING THE ACTION DOES:
    //
    //     if (this.target == null || this.target.isDeadOrEscaped()) {
    //         this.isDone = true; return;
    //     }
    //
    // A RESOLVE-TIME liveness read, ahead of the No Draw short-circuit, ahead of
    // the source onApplyPower hooks and ahead of the Artifact nullify -- so a
    // power aimed at a corpse costs nothing at all, not even an Artifact stack.
    //
    // WHY IT LANDS NOW (S2.28), and why its absence was invisible before: it is
    // the safety net the game relies on INSTEAD of guarding its queue-time walks,
    // and Acts 1-2 have no unguarded walk to protect. Act 3 has three -- Donu's
    // Circle of Protection (Donu.java:114-117), Deca's Square (Deca.java:122-128)
    // and Time Warp's Strength fan-out (TimeWarpPower.java:66-67) -- each of
    // which queues one item per monster RECORD with no liveness filter of any
    // kind. Their correctness is this early-out. The dossier's instruction not to
    // "fix" those loops into live-only walks is only sound with this guard in
    // place: queue-time and resolve-time liveness genuinely differ, and the game
    // reads the second.
    //
    // isDeadOrEscaped, NOT basically-dead: a HALF-DEAD monster is untargetable,
    // so a Deca that plated a half-dead ally would apply nothing.
    // monster_dead_or_escaped is exactly that predicate (combat_state.hpp).
    if (tgt != kActorPlayer && tgt < kMonsterCap &&
        monster_dead_or_escaped(s.monsters[tgt])) {
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
    // ChampionsBelt.java:32-35): the player owns it, the source IS the player,
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
    // (5) the list write (ApplyPowerAction.java:139-168): addPower's
    // stack-or-append, plus the re-sort that only THIS shape performs.
    add_power_to_list(s, tgt, id, amount, counter, is_source_monster,
                      applied_def, /*sort_after_append=*/true);
}

void add_power_direct(CombatState& s, uint8_t actor, PowerId id,
                      int amount) noexcept {
    // AbstractCreature.addPower (AbstractCreature.java:506-527), called DIRECTLY
    // -- `m.addPower(new StrengthPower(m, 1))` -- rather than through an
    // ApplyPowerAction. The body is the stack-or-append walk and NOTHING ELSE:
    //
    //   * NO liveness guard. `if (target.isDeadOrEscaped()) return` belongs to
    //     ApplyPowerAction.update (:97-100); addPower reaches a creature at 0 HP
    //     and a halfDead one alike. This is the load-bearing difference: the
    //     Darkling's REINCARNATE turn runs the relic onSpawnMonster loop
    //     synchronously at queue time (Darkling.java:134-136), while the record
    //     is still 0 HP / halfDead -- the heal is merely QUEUED at :131 -- and
    //     Philosopher's Stone's +1 Strength lands on it regardless. Routed
    //     through op_apply_power it was silently dropped, and a revived
    //     Darkling's Nip then hit for one less than the game's (captures
    //     STS239327 seq 407->408 and STS212624 seq 516->517, both A20 Act 3).
    //   * NO interception chain: no source onApplyPower, no Champion Belt, no
    //     Ginger/Turnip, no Artifact nullify (:106-138 are all ApplyPowerAction).
    //   * NO re-sort and NO onInitialApplication: both are the !hasBuffAlready
    //     tail of ApplyPowerAction.update (:165-168); addPower's append is a
    //     bare `powers.add` (:515). Any later ApplyPowerAction on the same
    //     creature re-sorts the whole list, which is why the omission is
    //     observable only as list ORDER in between, never as a wrong number.
    //
    // `counter` 0 and is_source_monster true: the constructed power's own ctor
    // still runs in the Java, so a duration debuff's justApplied latch would
    // still be derived -- but no direct-addPower site in landed content applies
    // one, and the two that exist (Philosopher's Stone, at battle start and at
    // spawn) apply Strength(+1).
    if (id == PowerId::NONE) {
        return;
    }
    add_power_to_list(s, actor, id, amount, /*counter=*/0,
                      /*is_source_monster=*/true, power_def(id),
                      /*sort_after_append=*/false);
}

namespace {

void add_power_to_list(CombatState& s, uint8_t tgt, PowerId id, int amount,
                       int counter, bool is_source_monster,
                       const PowerDef* applied_def,
                       bool sort_after_append) noexcept {
    PowerSlot* slots = nullptr;
    uint8_t* count = nullptr;
    if (!power_list_for(s, tgt, slots, count)) {
        return;
    }
    const uint16_t pid = static_cast<uint16_t>(id);
    // INSTANCED powers never merge. TheBombPower's ID is the literal "TheBomb"
    // concatenated with a static, ever-increasing counter (TheBombPower.java:
    // 31-32), so AbstractCreature.getPower never finds an existing instance and
    // ApplyPowerAction always adds a NEW AbstractPower -- each play is its own
    // fuse with its own damage. The registry marks such a power `instanced`
    // (powers.yaml -> PowerDef::instanced); the append path below is the whole
    // implementation.
    const bool instanced = applied_def != nullptr && applied_def->instanced;
    for (uint8_t i = 0; !instanced && i < *count; ++i) {
        if (slots[i].power_id == pid) {
            // PanachePower.stackPower (PanachePower.java:46-50) grows ONLY its
            // private damage: `this.damage += stackAmount`, with the 5-card
            // countdown in `amount` left exactly where it stood. Partial
            // progress therefore SURVIVES a re-application -- a second Panache
            // does not restart the count -- which is the opposite of the
            // AbstractPower default this branch implements for everything else.
            if (id == PowerId::PANACHE) {
                slots[i].counter =
                    static_cast<int16_t>(slots[i].counter + amount);
                return;
            }
            // AbstractPower.stackPower's FIRST line (AbstractPower.java:152-158):
            //     if (this.amount == -1) { logger.info(name + " does not stack");
            //                              return; }
            // A live slot holding -1 refuses every re-application. CONFUSION is
            // the only registered power that can REACH this branch holding -1:
            // its ctor assigns no amount at all, so the applied object carries
            // AbstractPower's field initializer (:65), which is what both its
            // producers pass -- Snecko Eye's atPreBattle (relics_boss.cpp) and
            // the Snecko's GLARE (monsters.yaml id 34) -- and those two CAN
            // co-occur, which is what makes the guard reachable rather than
            // theoretical. Barricade and Corruption also sit at -1 but never get
            // here: apply_is_a_no_op_repeat intercepts them at the top of this
            // function. Scoped to CONFUSION rather than written as a general
            // `slots[i].amount == -1` test, because every OTHER power that could
            // hold -1 OVERRIDES stackPower, and an override never consults the
            // base class's guard.
            if (id == PowerId::CONFUSION) {
                return;
            }
            // MalleablePower.stackPower (MalleablePower.java:79-82) is one of the
            // few overrides that touches BOTH numbers:
            //     this.amount += stackAmount; this.basePower += stackAmount;
            // basePower is PowerSlot.counter (power_malleable.hpp), so a
            // re-application permanently raises the end-of-turn RESET TARGET as
            // well as the live amount -- the opposite of Flight, whose
            // storedAmount stays frozen at the first instance's value. Falls
            // through to the ordinary additive `amount` update below rather than
            // returning, because the amount half IS the default. Unreachable in
            // S1/S2 (the Snake Plant is solo and applies it once,
            // SnakePlant.java:69-72); written because its absence would be a
            // silent wrong answer the day a second granter lands, not a missing
            // feature anyone would notice.
            if (id == PowerId::MALLEABLE) {
                slots[i].counter =
                    static_cast<int16_t>(slots[i].counter + amount);
            }
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
    // SLOT-CAP behaviour is UNCHANGED and deliberately so: a full 24-slot list
    // makes the application a silent no-op (the pre-existing contract at the top
    // of this function -- a malformed or extreme item must not crash). An
    // INSTANCED power reaches this the same way any other new slot does, so a
    // 25th simultaneous Bomb is simply not applied; no new behaviour is invented
    // here for it.
    if (*count >= kPowerCap) {
        return;
    }
    if (id == PowerId::CURL_UP && tgt < kMonsterCap) {
        // A newly-created CurlUpPower starts with triggered=false
        // (CurlUpPower.java:25,27-34). Existing instances preserve the latch when
        // stacked; only the new-slot path clears it.
        s.monsters[tgt].flags &= ~kMonsterFlagCurlUpTriggered;
    }
    if (id == PowerId::COMBUST && tgt == kActorPlayer) {
        s.flags = (s.flags & ~kCombatFlagCombustHpLossMask) |
                  (1u << kCombatFlagCombustHpLossShift);
    }
    // Whole-row assignment, not field-by-field: `counter`/`pad0` must be written
    // (not inherited) so a slot recycled after a removal cannot carry a stale
    // second number, and so byte-hashing stays padding-stable.
    PowerSlot fresh{};
    fresh.power_id = pid;
    fresh.amount = static_cast<int16_t>(amount);
    fresh.counter = static_cast<int16_t>(counter);
    if (id == PowerId::PANACHE) {
        // PanachePower's ctor (PanachePower.java:30-38) does NOT take the stack
        // amount as its amount: it hard-codes `this.amount = 5` (CARD_AMT, :27)
        // and takes the ctor argument as the private `damage`. Panache.use passes
        // the SAME magicNumber for both the ApplyPowerAction stack amount and the
        // ctor argument (Panache.java:32), so the item's `amount` IS the damage
        // and the countdown is the constant 5. That coincidence is why this row
        // authors no `counter:` operand.
        fresh.amount = kPanacheCardAmount;
        fresh.counter = static_cast<int16_t>(amount);
    }
    if (id == PowerId::MALLEABLE) {
        // MalleablePower's ctor (MalleablePower.java:28-37) sets `basePower =
        // amt` alongside `amount` itself -- a private field (:21) written at
        // construction and thereafter only by stackPower (:81). PowerSlot.counter
        // carries it, and atEndOfTurn resets `amount` to it every turn
        // (power_malleable.cpp). Same NEW-SLOT placement as Flight and Panache
        // above; unlike Flight's, the STACKING path also moves it (see the
        // MALLEABLE case in the stacking branch).
        fresh.counter = fresh.amount;
    }
    if (id == PowerId::FLIGHT) {
        // FlightPower's ctor (FlightPower.java:26-35) sets `storedAmount =
        // amount` alongside `amount` itself -- a PRIVATE field written once at
        // construction and never reassigned (:24,31). PowerSlot.counter carries
        // it, and atStartOfTurn restores `amount` to it every turn
        // (power_flight.cpp).
        //
        // ON THE NEW-SLOT PATH ONLY, exactly like Panache above and for the same
        // reason: stackPower is not overridden, so a re-application returns from
        // the stacking branch having added to the LIVE object's `amount` while
        // its `storedAmount` keeps the value the ORIGINAL instance was built
        // with. AbstractCreature.addPower hands the amount over and discards the
        // freshly constructed power, ctor field and all
        // (AbstractCreature.java:506-513). Writing the counter here too would
        // let a second application RAISE the refresh target, which the game
        // never does.
        fresh.counter = fresh.amount;
    }
    // justApplied for the three DURATION debuffs (Vulnerable / Weak / Frail).
    // Their ctors set it (VulnerablePower.java:36-38, WeakPower.java:35-37,
    // FrailPower.java:32-34) and the slot's own `counter` carries it, so it is
    // per-instance across the player and every monster. Only a NEW instance is
    // latched: stacking returned above, which is exactly ApplyPowerAction's
    // behaviour -- addPower hands the amount to the LIVE object and throws away
    // the freshly constructed one, latch and all (AbstractCreature.java:506-513).
    if (duration_debuff_starts_just_applied(
            s, tgt, id, is_source_monster)) {
        fresh.counter = 1;
    }
    // DrawReductionPower's IDENTICALLY-SHAPED latch (S2.28), and the reason it is
    // a separate branch rather than a fourth case in that predicate: the three
    // duration debuffs latch CONDITIONALLY (their ctors read turnHasEnded /
    // isSourceMonster), while this one is set by the FIELD INITIALIZER --
    // `private boolean justApplied = true;` (DrawReductionPower.java:17) -- so it
    // latches on EVERY new instance, unconditionally. Folding it into that
    // predicate would import three conditions the Java does not have here.
    //
    // NEW SLOT ONLY, the same placement as Flight / Panache / Malleable and the
    // duration debuffs, and for the same reason: the stacking branch returned
    // long before this line, which IS ApplyPowerAction's behaviour. So a second
    // Head Slam does not re-arm the skip -- the stack it just raised ticks down
    // at the end of that same round.
    if (id == PowerId::DRAW_REDUCTION) {
        fresh.counter = 1;
    }
    // IntangiblePower's (the MONSTER class, id 107) IDENTICALLY-SHAPED latch
    // (S2.27). `this.justApplied = true` is the LAST line of the ctor
    // (IntangiblePower.java:33) -- unconditional, like Draw Reduction's field
    // initializer and unlike the three duration debuffs' conditional latches --
    // so it gets its own branch for the same reason id 111 does rather than a
    // fifth case in duration_debuff_starts_just_applied.
    //
    // NEW SLOT ONLY, the same placement as every latch above. The stacking path
    // returned long ago, which is ApplyPowerAction's behaviour; here it is
    // additionally unreachable, because the only producer re-applies exclusively
    // when the monster does NOT already hold the power (Nemesis.java:114).
    //
    // NOTE THE SIBLING ROW IS UNTOUCHED: PowerId::INTANGIBLE (id 29,
    // "IntangiblePlayer") has NO justApplied field and must not get one.
    if (id == PowerId::INTANGIBLE_MONSTER) {
        fresh.counter = 1;
    }
    slots[*count] = fresh;
    ++*count;
    // The new-power branch of ApplyPowerAction ends with the whole-list re-sort
    // (ApplyPowerAction.java:165-167). Slot order is load-bearing: it is the
    // iteration order of compute_damage's atDamage* walks and of every
    // per-power hook fan-out, so Weak (99) lands behind Strength (5) no matter
    // which was applied first -- (base + Str) * 0.75, never (base * 0.75) + Str.
    // The bare addPower shape (add_power_direct) does NOT sort: AbstractCreature.
    // addPower:515 is a plain `powers.add`.
    if (sort_after_append) {
        sort_powers_like_the_game(slots, *count);
    }
}

}  // namespace

// REMOVE_POWER (RemoveSpecificPowerAction): drop PowerId(flags low16) from tgt's
// power list (shifting the tail down). No-op if the actor lacks the power.
// `flags` may additionally carry an INSTANCE KEY (interp.hpp) naming one
// specific slot of an instanced power; without one this is the historical
// first-match-by-id.
void op_remove_power(CombatState& s, uint8_t tgt, PowerId id,
                     uint32_t flags) noexcept {
    PowerSlot* slots = nullptr;
    uint8_t* count = nullptr;
    if (!power_list_for(s, tgt, slots, count)) {
        return;
    }
    const uint8_t i =
        find_target_slot(slots, *count, static_cast<uint16_t>(id), flags);
    if (i >= *count) {
        return;
    }
    remove_slot_at(s, tgt, slots, count, i);
}

// REMOVE_DEBUFFS (RemoveDebuffsAction.update, RemoveDebuffsAction.java:23-30):
//
//     for (AbstractPower p : this.c.powers) {
//         if (p.type != PowerType.DEBUFF) continue;
//         this.addToTop(new RemoveSpecificPowerAction(this.c, this.c, p.ID));
//     }
//     this.isDone = true;
//
// THE ENUMERATION IS THE OPCODE. It happens when this action RESOLVES, over the
// power list as it then stands -- which is exactly what a queue-time expansion
// into N REMOVE_POWER items could not express, because Orange Pellets queues
// addToBot, behind the played card's own actions, so a debuff can land in
// between.
//
// The DEBUFF test reads the LIVE INSTANCE's type. StrengthPower.updateDescription
// (StrengthPower.java:81-89) and DexterityPower's (:74-82) recompute
// `this.type = amount > 0 ? BUFF : DEBUFF`, so a negative Strength stack IS a
// debuff and is removed, while a positive one is not. That is the same two-term
// predicate op_apply_power builds at apply time (negative_stat_flip above), and
// it is spelled once here rather than re-derived.
//
// Two consequences worth stating because they look like bugs and are not:
//   * Shackled (GainStrengthPower) is DEBUFF-typed in the Java
//     (GainStrengthPower.java:29), so this removes it -- and with it the pending
//     Strength restoration. Reproduced, not corrected.
//   * addToTop per power means the removals resolve in REVERSE list order. No
//     removal's side effects are read by another today, so it is unobservable;
//     it is done anyway because it is free and stays right if that changes.
void op_remove_debuffs(CombatState& s, uint8_t tgt) noexcept {
    PowerSlot* slots = nullptr;
    uint8_t* count = nullptr;
    if (!power_list_for(s, tgt, slots, count)) {
        return;
    }
    for (uint8_t i = 0; i < *count; ++i) {
        const PowerId id = static_cast<PowerId>(slots[i].power_id);
        const PowerDef* def = power_def(id);
        const bool negative_stat_flip =
            (id == PowerId::STRENGTH || id == PowerId::DEXTERITY) &&
            slots[i].amount <= 0;
        const bool is_debuff =
            (def != nullptr && def->type == PowerType::DEBUFF) ||
            negative_stat_flip;
        if (!is_debuff) {
            continue;  // `if (p.type != DEBUFF) continue;` (:25)
        }
        ActionQueueItem rem{};
        rem.opcode = static_cast<uint16_t>(Opcode::REMOVE_POWER);
        rem.src = tgt;
        rem.tgt = tgt;
        rem.flags = make_apply_power_flags(id);
        add_to_top(s, rem);  // addToTop (:26)
    }
}

// REDUCE_POWER (ReducePowerAction): subtract `amount` from one power and remove
// the slot when it reaches zero. Kept as a queued opcode so an atEndOfRound power
// cannot mutate/compact the list while the dispatcher is still iterating it.
//
// ReducePowerAction.update (:36-53) resolves its victim ONCE -- by id
// (getPower(powerID)) or by the exact INSTANCE it was handed (:39-43) -- and then
// either reducePower or, when the reduction would not leave a positive amount,
// addToTop RemoveSpecificPowerAction on THAT object (:45-51). The instance-key
// path here is that second constructor; see interp.hpp for why the key is the
// slot's own {amount, counter} rather than its index.
void op_reduce_power(CombatState& s, uint8_t tgt, PowerId id, int amount,
                     uint32_t flags) noexcept {
    if (amount <= 0) {
        return;
    }
    PowerSlot* slots = nullptr;
    uint8_t* count = nullptr;
    if (!power_list_for(s, tgt, slots, count)) {
        return;
    }
    const uint8_t i =
        find_target_slot(slots, *count, static_cast<uint16_t>(id), flags);
    if (i >= *count) {
        return;
    }
    slots[i].amount = static_cast<int16_t>(slots[i].amount - amount);
    if (slots[i].amount <= 0) {
        // The SAME slot, by index -- not a second first-match-by-id lookup, which
        // would pick the wrong instance of an instanced power.
        remove_slot_at(s, tgt, slots, count, i);
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
