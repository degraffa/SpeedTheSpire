// Action-queue mechanics + getNextAction pump. See action_queue.hpp for the
// design/provenance notes, including the two places our model diverges from a
// naive reading of the source (preTurnActions storage gap; monster_attacks_queued
// reset placement).
//
// Provenance: GameActionManager.getNextAction (GameActionManager.java:185-367),
// addToBottom/addToTop/addToTurnStart (96-100, 139-149), addCardQueueItem
// (102-116), callEndOfTurnActions (369-377). Design doc §5.1-§5.4, §10 trap 9.

#include "sts/engine/action_queue.hpp"

#include <cassert>

#include "sts/engine/card_play.hpp"  // resolve_card_play wired into pump_step step 3
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"  // execute_opcode wired into pump_step
#include "sts/engine/power_hooks.hpp"  // B3.2: start/end-of-turn power dispatch
#include "sts/engine/relic_hooks.hpp"  // B3.24: start/end-of-turn relic dispatch

namespace sts::engine {

// The start-of-turn DrawCardAction (start_of_turn below) queues kOpcodeDrawCard,
// which must equal the interpreter's real DRAW opcode so the queued item
// actually draws when popped. Kept in lockstep here.
static_assert(kOpcodeDrawCard == static_cast<uint16_t>(Opcode::DRAW),
              "start-of-turn DrawCard opcode must match interp.hpp Opcode::DRAW");

namespace {

// Any monster in a live slot (design doc §5.2 uses areMonstersBasicallyDead();
// the proxy here is hp > 0 -- MonsterState has no halfDead/escaped fields, so
// the closest available liveness signal is positive HP).
[[nodiscard]] bool any_monster_alive(const CombatState& s) noexcept {
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        if (s.monsters[i].hp > 0) {
            return true;
        }
    }
    return false;
}

// queueMonsters equivalent (GameActionManager.java:306 ->
// MonsterGroup.queueMonsters): enqueue every live monster, in slot order. The
// game skips dead/escaped monsters; the proxy here is hp > 0 (see above).
void queue_monsters(CombatState& s) noexcept {
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        if (s.monsters[i].hp > 0) {
            assert(s.monster_queue_count < kMonsterQueueCap &&
                   "monster_queue overflow (design doc §4.1: hard assert)");
            s.monster_queue[s.monster_queue_count].monster_index = i;
            s.monster_queue[s.monster_queue_count].flags = 0;
            ++s.monster_queue_count;
        }
    }
}

// Pop the head (index 0) of the card queue, shifting the tail down one slot
// (ArrayList.remove(0); GameActionManager.java:298,325 style).
void card_queue_pop_front(CombatState& s) noexcept {
    assert(s.card_queue_count > 0);
    for (uint8_t i = 0; i + 1 < s.card_queue_count; ++i) {
        s.card_queue[i] = s.card_queue[i + 1];
    }
    --s.card_queue_count;
}

// Pop the head (index 0) of the monster queue (remove(0)).
void monster_queue_pop_front(CombatState& s) noexcept {
    assert(s.monster_queue_count > 0);
    for (uint8_t i = 0; i + 1 < s.monster_queue_count; ++i) {
        s.monster_queue[i] = s.monster_queue[i + 1];
    }
    --s.monster_queue_count;
}

// callEndOfTurnActions (GameActionManager.java:369-377 / design doc §5.4).
// Every listener in the skeleton is a no-op: no relics, no orbs, and the three
// skeleton powers (Strength/Vulnerable/Weak) have no end-of-turn hook; hand
// cards' triggerOnEndOfTurnForPlayingCard (Burn/Regret/Decay) are not in scope.
// The sequence's *structure* lives here as a documented stub so future listeners
// can attach without moving the call site.
void call_end_of_turn_actions(CombatState& s) noexcept {
    // Frozen §5.4 order (GameActionManager.callEndOfTurnActions:369-377):
    //   applyEndOfTurnRelics -> relics onPlayerEndTurn (acq order; Orichalcum), B3.24
    //   applyEndOfTurnPreCardPowers             -- Metallicize (atEndOfTurnPreEndTurnCards)
    //   TriggerEndOfTurnOrbsAction               -- no orbs
    //   hand cards triggerOnEndOfTurnForPlayingCard -- Burn/Regret/Decay (card-level, B3.9)
    //   stance.onEndOfTurn                       -- stanceless
    // then applyEndOfTurnTriggers (Combust atEndOfTurn) fires via the queued
    // discard sequence (AbstractCreature.java:548-553) -- AFTER the pre-card
    // powers and hand triggers. All queue via add_to_bottom, so call order ==
    // resolution order: Metallicize block lands before Combust's HP loss/damage.
    {
        const RelicView rv = player_relics(s);  // applyEndOfTurnRelics (B3.24)
        dispatch_relics_on_player_end_turn(s, rv.relics, rv.count);
    }
    dispatch_at_end_of_turn_pre_card(s);   // Metallicize
    // hand-card end-of-turn triggers (Burn/Decay) -- card-level, B3.9 stub.
    // stance.onEndOfTurn -- stanceless stub.
    dispatch_at_end_of_turn(s);            // Combust
    // All no-op unless a hook-bearing power is present (fixtures unchanged).
}

// Start-of-turn sequence (design doc §5.2 step 6; GameActionManager.java:
// 329-366). Stubs are named where a future subsystem would attach.
void start_of_turn(CombatState& s) noexcept {
    // monsters' applyEndOfTurnPowers -- stub (no monster powers with this hook).
    s.cards_played_this_turn = 0;               // player.cardsPlayedThisTurn = 0
    // orbsChanneledThisTurn.clear() -- no orbs.
    // applyStartOfTurnRelics -> relics atTurnStart (acq order; Happy Flower, Lantern,
    // B3.24). PreDrawCards -- card-level hooks not in scope.
    {
        const RelicView rv = player_relics(s);
        dispatch_relics_at_turn_start(s, rv.relics, rv.count);
    }
    // applyStartOfTurnPowers (§5.2 step 6, pre-draw): Berserk/Mayhem/Magnetism
    // energy/play; applyStartOfTurnOrbs -- no orbs. No-op without such a power.
    dispatch_at_start_of_turn(s);

    // Energy recharge (EnergyManager.recharge(); see kIroncladBaseEnergy in
    // action_queue.hpp). The real game performs this every turn via a
    // presentation-coupled effect that still affects outcomes, so it is in scope.
    // SET, not additive: any unspent energy from the previous turn does not carry
    // over.
    s.player_energy = kIroncladBaseEnergy;
    // NOTE: monster_attacks_queued is deliberately NOT reset here -- it is
    // cleared at the end-turn sentinel instead (see action_queue.hpp note (2)).
    s.turn_has_ended = 0;                        // this.turnHasEnded = false
    ++s.turn;                                    // ++turn

    // Block decay (GameActionManager.java:353-359). Barricade/Blur keep block;
    // Calipers loses 15 instead of zeroing. The skeleton has none of these, so
    // the branch STRUCTURE is present but only the default path runs.
    const bool has_barricade = false;  // future: player has Barricade power
    const bool has_blur = false;       // future: player has Blur power
    const bool has_calipers = false;   // future: player has Calipers relic
    if (!has_barricade && !has_blur) {
        if (!has_calipers) {
            s.player_block = 0;                                  // loseBlock()
        } else {
            s.player_block = static_cast<int16_t>(               // loseBlock(15)
                s.player_block > 15 ? s.player_block - 15 : 0);
        }
    }

    // Queue DrawCardAction(gameHandSize) (line 361). The pump only enqueues a
    // well-formed item here; the DRAW opcode does the drawing when it is popped.
    ActionQueueItem draw{};
    draw.opcode = kOpcodeDrawCard;
    draw.src = kActorPlayer;
    draw.tgt = kActorPlayer;
    draw.amount = kStartOfTurnDrawCount;
    draw.flags = 0;
    add_to_bottom(s, draw);
    // applyStartOfTurnPostDrawRelics -- none. applyStartOfTurnPostDrawPowers
    // (Brutality/Demon Form): queued AFTER the DrawCardAction (line 362-363), so
    // its effects resolve after the draw. No-op without such a power.
    dispatch_at_start_of_turn_post_draw(s);
    // EnableEndTurnButtonAction (line 364) is modeled by step 7 handing control
    // back to the player once the queued DrawCard has drained; no separate item.
}

}  // namespace

// --- Monster-turn extension point -------------------------------------------

void default_monster_turn(CombatState& /*state*/,
                          uint8_t /*monster_index*/) noexcept {
    // No-op default monster turn (the extension point when no AI is supplied).
    // jaw_worm_take_turn (monster_jaw_worm.cpp) provides the real Jaw Worm turn.
}

// --- Queue insertion primitives ---------------------------------------------

void add_to_bottom(CombatState& s, ActionQueueItem item) noexcept {
    assert(s.action_count < kActionQueueCap &&
           "action_queue overflow (design doc §4.1: hard assert)");
    s.action_queue[s.action_tail] = item;
    s.action_tail = static_cast<uint8_t>((s.action_tail + 1) % kActionQueueCap);
    ++s.action_count;
}

void add_to_top(CombatState& s, ActionQueueItem item) noexcept {
    assert(s.action_count < kActionQueueCap &&
           "action_queue overflow (design doc §4.1: hard assert)");
    s.action_head =
        static_cast<uint8_t>((s.action_head + kActionQueueCap - 1) %
                             kActionQueueCap);
    s.action_queue[s.action_head] = item;
    ++s.action_count;
}

void add_to_turn_start(CombatState& s, ActionQueueItem item) noexcept {
    assert(s.pre_turn_count < kPreTurnActionQueueCap &&
           "pre_turn_actions overflow (design doc §4.1: hard assert)");
    s.pre_turn_head =
        static_cast<uint8_t>((s.pre_turn_head + kPreTurnActionQueueCap - 1) %
                             kPreTurnActionQueueCap);
    s.pre_turn_actions[s.pre_turn_head] = item;
    ++s.pre_turn_count;
}

void add_card_to_queue_bottom(CombatState& s, CardQueueItem item) noexcept {
    assert(s.card_queue_count < kCardQueueCap &&
           "card_queue overflow (design doc §4.1: hard assert)");
    s.card_queue[s.card_queue_count] = item;
    ++s.card_queue_count;
}

void add_card_to_queue_top(CombatState& s, CardQueueItem item) noexcept {
    assert(s.card_queue_count < kCardQueueCap &&
           "card_queue overflow (design doc §4.1: hard assert)");
    // TRAP 9 (design doc §10 item 9; GameActionManager.java:102-108): when the
    // queue is non-empty the new item goes to index 1 (the currently-resolving
    // head stays at index 0); only an empty queue takes it at index 0.
    if (s.card_queue_count == 0) {
        s.card_queue[0] = item;
    } else {
        for (uint8_t i = s.card_queue_count; i >= 2; --i) {
            s.card_queue[i] = s.card_queue[i - 1];
        }
        s.card_queue[1] = item;
    }
    ++s.card_queue_count;
}

CardQueueItem make_end_turn_sentinel() noexcept {
    CardQueueItem c{};
    c.card_index = kEndTurnSentinel;
    c.target = 0;
    return c;
}

bool is_end_turn_sentinel(CardQueueItem item) noexcept {
    return item.card_index == kEndTurnSentinel;
}

// --- Low-level ring pops -----------------------------------------------------

bool pop_action_front(CombatState& s, ActionQueueItem& out) noexcept {
    if (s.action_count == 0) {
        return false;
    }
    out = s.action_queue[s.action_head];
    s.action_head = static_cast<uint8_t>((s.action_head + 1) % kActionQueueCap);
    --s.action_count;
    return true;
}

bool pop_pre_turn_front(CombatState& s, ActionQueueItem& out) noexcept {
    if (s.pre_turn_count == 0) {
        return false;
    }
    out = s.pre_turn_actions[s.pre_turn_head];
    s.pre_turn_head =
        static_cast<uint8_t>((s.pre_turn_head + 1) % kPreTurnActionQueueCap);
    --s.pre_turn_count;
    return true;
}

// --- Pump --------------------------------------------------------------------

PumpStepResult pump_step(CombatState& s, MonsterTurnFn take_turn) noexcept {
    PumpStepResult r{};

    // Minimal combat-over check (design doc §5.2 scope note: full death handling
    // is not yet modeled; this just gives the phase transition so pump() cannot
    // spin).
    if (s.player_hp <= 0 || !any_monster_alive(s)) {
        s.phase = static_cast<uint8_t>(CombatPhase::COMBAT_OVER);
        r.outcome = PumpOutcome::COMBAT_OVER;
        return r;
    }
    s.phase = static_cast<uint8_t>(CombatPhase::RESOLVING);

    // 1. actions non-empty -> pop front, execute. The popped item is dispatched
    //    through the effect interpreter via execute_opcode (NOP/unrecognized
    //    opcodes are safe no-ops, so a value-init'd item still drains harmlessly).
    if (s.action_count > 0) {
        pop_action_front(s, r.executed);
        execute_opcode(s, r.executed);
        r.outcome = PumpOutcome::RAN_ACTION;
        return r;
    }

    // 2. else preTurnActions non-empty -> pop front, execute (dispatched too).
    if (s.pre_turn_count > 0) {
        pop_pre_turn_front(s, r.executed);
        execute_opcode(s, r.executed);
        r.outcome = PumpOutcome::RAN_PRE_TURN;
        return r;
    }

    // 3. else cardQueue non-empty -> resolve head. Either the end-turn sentinel
    //    (null-card) or a real card play (§5.3), resolved by
    //    resolve_card_play (card_play.cpp): it runs the no-op hook stubs,
    //    ++cards_played_this_turn, the trap-10 target resolution, queues the
    //    card's effect actions via add_to_bottom (they resolve on later pump
    //    iterations via step 1), moves the card hand->discard, and deducts energy.
    //    The skeleton's cards never insert into the cardQueue during use(), so
    //    popping the head before resolving is safe (trap 9's index-1 rule is
    //    untouched -- no card re-enters the queue mid-resolution).
    if (s.card_queue_count > 0) {
        const CardQueueItem head = s.card_queue[0];
        card_queue_pop_front(s);
        if (is_end_turn_sentinel(head)) {
            s.turn_has_ended = 1;              // (endTurn(): turnHasEnded = true)
            s.monster_attacks_queued = 0;      // prime step 4 (see hpp note (2))
            call_end_of_turn_actions(s);       // §5.4 stub sequence
            r.outcome = PumpOutcome::END_TURN_SENTINEL;
        } else {
            resolve_card_play(s, head);        // (§5.3): dequeue-resolve
            r.outcome = PumpOutcome::RAN_CARD_QUEUE;
        }
        return r;
    }

    // 4. else if !monsterAttacksQueued -> set it, queue all live monsters.
    if (s.monster_attacks_queued == 0) {
        s.monster_attacks_queued = 1;
        // skipMonsterTurn (GameActionManager.java:305) is tied to mechanics the
        // skeleton lacks (e.g. Entangled); future extension point, not queued.
        queue_monsters(s);
        r.outcome = PumpOutcome::QUEUED_MONSTERS;
        return r;
    }

    // 5. else monsterQueue non-empty -> pop head; if alive, take turn + apply
    //    turn powers. take_turn is the monster-turn seam.
    if (s.monster_queue_count > 0) {
        const uint8_t mi = s.monster_queue[0].monster_index;
        monster_queue_pop_front(s);
        if (mi < s.monster_count && s.monsters[mi].hp > 0) {
            take_turn(s, mi);            // m.takeTurn()
            // m.applyTurnPowers() -- stub (no monster powers with a turn hook).
        }
        r.monster_index = mi;
        r.outcome = PumpOutcome::RAN_MONSTER;
        return r;
    }

    // 6. else if turnHasEnded and monsters alive -> start-of-turn sequence.
    if (s.turn_has_ended && any_monster_alive(s)) {
        start_of_turn(s);
        r.outcome = PumpOutcome::STARTED_TURN;
        return r;
    }

    // 7. else -> control returns to the player.
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    r.outcome = PumpOutcome::WAITING_ON_USER;
    return r;
}

void pump(CombatState& s, MonsterTurnFn take_turn) noexcept {
    for (;;) {
        const PumpStepResult r = pump_step(s, take_turn);
        if (r.outcome == PumpOutcome::WAITING_ON_USER ||
            r.outcome == PumpOutcome::COMBAT_OVER) {
            return;
        }
    }
}

}  // namespace sts::engine
