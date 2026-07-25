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
#include "sts/engine/piles.hpp"   // reset_cost_for_turn (end-turn sweep)
#include "sts/engine/power_hooks.hpp"  // start/end-of-turn power dispatch
#include "sts/engine/relic_hooks.hpp"  // start/end-of-turn relic dispatch

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
    //   applyEndOfTurnRelics -> relics onPlayerEndTurn (acq order; Orichalcum)
    //   applyEndOfTurnPreCardPowers             -- Metallicize (atEndOfTurnPreEndTurnCards)
    //   TriggerEndOfTurnOrbsAction               -- no orbs
    //   hand cards triggerOnEndOfTurnForPlayingCard -- Burn/Regret/Decay (card-level)
    //   stance.onEndOfTurn                       -- stanceless
    // then applyEndOfTurnTriggers (Combust atEndOfTurn) fires via the queued
    // discard sequence (AbstractCreature.java:548-553) -- AFTER the pre-card
    // powers and hand triggers. All queue via add_to_bottom, so call order ==
    // resolution order: Metallicize block lands before Combust's HP loss/damage.
    {
        const RelicView rv = player_relics(s);  // applyEndOfTurnRelics
        dispatch_relics_on_player_end_turn(s, rv.relics, rv.count);
    }
    dispatch_at_end_of_turn_pre_card(s);   // Metallicize
    // Hand-card end-of-turn triggers (Burn/Decay/Doubt/Regret/Shame) queue here,
    // before at-end-of-turn powers. The later DiscardAtEndOfTurnAction sweep is a
    // separate bottom-queued action, so trigger effects see the full hand first.
    dispatch_card_end_of_turn(s);
    // stance.onEndOfTurn -- stanceless stub.
    dispatch_at_end_of_turn(s);            // Combust
    // DiscardAtEndOfTurnAction follows the sentinel's card/power effects. Its
    // ethereal sweep and ordinary hand discard are collapsed into one action.
    ActionQueueItem discard_hand{};
    discard_hand.opcode = static_cast<uint16_t>(Opcode::DISCARD_HAND);
    discard_hand.src = kActorPlayer;
    discard_hand.tgt = kActorPlayer;
    add_to_bottom(s, discard_hand);
    // All no-op unless a hook-bearing power is present (fixtures unchanged).
}

// Which of the game's two call sites is running the start-of-turn sequence.
// They are NOT the same sequence: the combat-start block (AbstractRoom.java:
// 236-258) omits the end-of-round pass that getNextAction's step 6 opens with --
// see begin_first_turn below for why the game can never reach step 6 on turn 1.
enum class TurnStart : uint8_t {
    kCombatStart,      // AbstractRoom.update turn-1 block (AbstractRoom.java:236-258)
    kSubsequentTurn,   // getNextAction step 6 (GameActionManager.java:329-366)
};

// Start-of-turn sequence (design doc §5.2 step 6; GameActionManager.java:
// 329-366). Stubs are named where a future subsystem would attach.
void start_of_turn(CombatState& s, TurnStart when) noexcept {
    // monsters.applyEndOfTurnPowers() (GameActionManager.java:331) -- the
    // atEndOfRound dispatch (monster atEndOfTurn -> player atEndOfRound -> monster
    // atEndOfRound). The Cultist's Ritual Strength ramp fires here. No-op unless a
    // power binds these hooks, so jaw-worm-only fixtures are byte-identical.
    //
    // It belongs to step 6 ALONE. The combat-start block has no counterpart line,
    // so a round has to have ended for this to run -- running it while priming
    // turn 1 gives every power that binds an end-of-round hook and is present at
    // combat start one free trigger before the player acts.
    if (when == TurnStart::kSubsequentTurn) {
        dispatch_at_end_of_round(s);
    }
    s.cards_played_this_turn = 0;               // player.cardsPlayedThisTurn = 0
    // orbsChanneledThisTurn.clear() -- no orbs.
    // applyStartOfTurnRelics -> relics atTurnStart (acq order; Happy Flower, Lantern,
    // relics). PreDrawCards -- card-level hooks not in scope.
    {
        const RelicView rv = player_relics(s);
        dispatch_relics_at_turn_start(s, rv.relics, rv.count);
    }
    // applyStartOfTurnPowers (§5.2 step 6, pre-draw): Berserk/Mayhem/Magnetism
    // energy/play; applyStartOfTurnOrbs -- no orbs. No-op without such a power.
    dispatch_at_start_of_turn(s);

    // Energy recharge (EnergyManager.recharge(), EnergyManager.java:25-40; see
    // kIroncladBaseEnergy in action_queue.hpp). The real game performs this every
    // turn via a presentation-coupled effect that still affects outcomes, so it
    // is in scope.
    //
    // Two branches, not one. The default is EnergyPanel.setEnergy(energyMaster)
    // -- a SET, so unspent energy does NOT carry over. With Ice Cream owned it is
    // EnergyPanel.addEnergy(energyMaster) instead: an ADD, capped at 999
    // (EnergyPanel.addEnergy, EnergyPanel.java:59-68). The third branch, the
    // Conserve power, is Silent-only and has no registry row. Retires the
    // stage-a SET-to-constant simplification; the no-relic path below is
    // byte-identical to the unconditional SET it replaces.
    //
    // The Ice Cream branch is kSubsequentTurn ONLY, and the derivation is a chain
    // worth spelling out because the two call sites reach energy by different
    // routes entirely:
    //   * Ice Cream lives in EnergyManager.recharge (EnergyManager.java:26-34),
    //     and recharge() has exactly ONE caller in the whole game: the
    //     PlayerTurnEffect constructor (PlayerTurnEffect.java:46).
    //   * PlayerTurnEffect is constructed only by DrawCardAction's 3-arg ctor
    //     when `endTurnDraw` is true (DrawCardAction.java:32-34).
    //   * Step 6 passes true (`new DrawCardAction(null, gameHandSize, true)`,
    //     GameActionManager.java:361) -- so a later turn DOES run recharge().
    //   * The combat-start block passes the 2-arg ctor (`new DrawCardAction(
    //     player, gameHandSize)`, AbstractRoom.java:242), which delegates with
    //     endTurnDraw = false (DrawCardAction.java:41-43) -- so turn 1 NEVER
    //     reaches recharge(), and Ice Cream's ADD is unreachable there.
    // Turn 1's energy comes from GainEnergyAndEnableControlsAction instead
    // (AbstractRoom.java:241), whose update calls player.gainEnergy(energyMaster)
    // = EnergyPanel.addEnergy (AbstractPlayer.java:1555-1558) with NO Ice Cream
    // test -- an add onto a panel EnergyManager.prep() has just zeroed
    // (EnergyManager.java:21-24), i.e. exactly the plain SET below.
    if (when == TurnStart::kSubsequentTurn &&
        player_has_relic(s, RelicId::ICE_CREAM)) {
        int32_t charged =
            static_cast<int32_t>(s.player_energy) + kIroncladBaseEnergy;
        if (charged > 999) {
            charged = 999;
        }
        s.player_energy = static_cast<int16_t>(charged);
    } else {
        s.player_energy = kIroncladBaseEnergy;
    }
    // NOTE: monster_attacks_queued is deliberately NOT reset here -- it is
    // cleared at the end-turn sentinel instead (see action_queue.hpp note (2)).
    s.turn_has_ended = 0;                        // this.turnHasEnded = false
    ++s.turn;                                    // ++turn

    // Block decay (GameActionManager.java:353-359). Barricade/Blur keep block;
    // Calipers loses 15 (AbstractCreature.loseBlock(int), clamped at 0) instead
    // of zeroing. Calipers is a registered relic and Barricade is now a
    // registered power, so both of those branches are LIVE; Blur is Silent-only
    // and out of S1 scope, so its branch structure is present and never taken.
    //
    // kSubsequentTurn ONLY. This whole paragraph is step 6's; the combat-start
    // block (AbstractRoom.java:236-258) has no loseBlock line of any kind, which
    // stands to reason -- block is a per-turn resource and turn 1 has no previous
    // turn to decay. It is INERT today either way (both entry points reach
    // begin_first_turn with player_block == 0, advance.cpp / run_advance.cpp), so
    // gating it moves no fixture; it is gated because leaving a step-6-only line
    // running at combat start is exactly the shape of the end-of-round bug this
    // fix exists to remove, and it would come alive the moment anything grants
    // block before turn 1 (a Barricade-like power, or an atBattleStart block
    // relic) -- at which point turn 1 would silently eat it.
    //
    // BARRICADE SITS ON THIS SIDE OF THE TURN-1 GATE, with Calipers, and the Java
    // leaves no choice: the whole `if (!hasPower("Barricade") && !hasPower("Blur"))
    // { if (!hasRelic("Calipers")) loseBlock(); else loseBlock(15); }` paragraph is
    // GameActionManager.java:353-359, inside getNextAction's step-6 branch, and the
    // combat-start block (AbstractRoom.java:236-258) contains no loseBlock line of
    // any kind. Barricade is not a rule that ADDS something at turn start; it is a
    // guard on a decay that only step 6 performs, so on the combat-start side there
    // is nothing for it to guard. Putting it on the other side of the gate would
    // also be unobservable-by-construction and therefore untestable.
    if (when == TurnStart::kSubsequentTurn) {
        // hasPower is a PRESENCE test: BarricadePower's amount is the -1 marker
        // its ctor sets (BarricadePower.java:22), never a positive stack.
        bool has_barricade = false;
        for (uint8_t i = 0; i < s.player_power_count; ++i) {
            if (s.player_powers[i].power_id ==
                static_cast<uint16_t>(PowerId::BARRICADE)) {
                has_barricade = true;
                break;
            }
        }
        const bool has_blur = false;       // Silent-only; no registry row in S1
        const bool has_calipers = player_has_relic(s, RelicId::CALIPERS);
        if (!has_barricade && !has_blur) {
            if (!has_calipers) {
                s.player_block = 0;                              // loseBlock()
            } else {
                s.player_block = static_cast<int16_t>(           // loseBlock(15)
                    s.player_block > 15 ? s.player_block - 15 : 0);
            }
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
    // applyStartOfTurnPostDrawRelics then applyStartOfTurnPostDrawPowers
    // (GameActionManager.java:361-363), both queued AFTER the DrawCardAction so
    // their effects resolve after the draw. Relics first, in acquisition order
    // (Pocketwatch's conditional 3-card draw; Gambling Chip binds this hook too,
    // with a deferred body). No-op without such a relic or power.
    //
    // The RELIC phase is deliberately UNGATED -- unlike the end-of-round pass,
    // the Ice Cream branch and the block decay above, applyStartOfTurnPostDrawRelics
    // is one of the lines the combat-start block DOES carry (AbstractRoom.java:254,
    // alongside applyStartOfTurnRelics at :253), so it belongs to every turn.
    {
        const RelicView rv = player_relics(s);
        dispatch_relics_at_turn_start_post_draw(s, rv.relics, rv.count);
    }
    // The POWER phase is a known asymmetry, deliberately left UNGATED here: the
    // combat-start block calls applyStartOfTurnPowers (AbstractRoom.java:256) but
    // has no applyStartOfTurnPostDrawPowers line, so strictly this call belongs to
    // step 6 alone (GameActionManager.java:363). It long predates the TurnStart
    // split above and is inert -- no registered power binds the post-draw hook --
    // so changing it is a behaviour question that wants its own test, not a
    // silent edit. Recorded here rather than changed or ignored.
    dispatch_at_start_of_turn_post_draw(s);
    // EnableEndTurnButtonAction (line 364) is modeled by step 7 handing control
    // back to the player once the queued DrawCard has drained; no separate item.
}

}  // namespace

// --- Combat start ------------------------------------------------------------

void begin_first_turn(CombatState& s, MonsterTurnFn take_turn) noexcept {
    // clear() (GameActionManager.java:431-433) opens a combat at turn 1 with
    // turnHasEnded false; monsterAttacksQueued is true from its field initializer
    // (GameActionManager.java:76) and only endTurn clears it. Here turn starts at
    // 0 so the shared sequence's ++turn lands it on 1.
    s.turn = 0;
    s.monster_attacks_queued = 1;
    // Deliberately NOT 1: with turnHasEnded false the pump below cannot take the
    // step-6 branch, which is exactly the game's situation on turn 1.
    s.turn_has_ended = 0;

    // AbstractRoom.java:229-235 -- while anything is queued, update() drains it and
    // waitTimer does not tick, so usePreBattleAction's actions resolve BEFORE the
    // turn-1 block. monsterAttacksQueued is already true, so step 4 cannot queue a
    // monster turn ahead of the player.
    pump(s, take_turn);
    if (s.phase == static_cast<uint8_t>(CombatPhase::COMBAT_OVER)) {
        return;
    }

    // The turn-1 block itself (AbstractRoom.java:236-258): the SAME start-of-turn
    // machinery every later turn runs, minus the end-of-round pass -- energy,
    // start-of-turn relics/powers, ++turn, the opening DrawCardAction.
    start_of_turn(s, TurnStart::kCombatStart);
    pump(s, take_turn);  // resolve the queued opening draw -> WAITING_ON_USER
}

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
    // spin). The all-monsters-dead victory branch is gated on the cannotLose
    // latch: between a splitting slime's SUICIDE and its children's
    // SPAWN_MONSTER actions no monster is alive, and the game keeps the battle
    // open exactly because endBattle() checks !cannotLose (AbstractMonster.
    // updateDeathAnimation:869; CannotLoseAction/CanLoseAction:12-15). Player
    // death is NOT gated -- the Java latch only guards the victory branch.
    if (s.player_hp <= 0 ||
        (!any_monster_alive(s) && (s.flags & kCombatFlagCannotLose) == 0u)) {
        s.phase = static_cast<uint8_t>(CombatPhase::COMBAT_OVER);
        r.outcome = PumpOutcome::COMBAT_OVER;
        return r;
    }
    s.phase = static_cast<uint8_t>(CombatPhase::RESOLVING);

    // 1. actions non-empty -> pop front, execute. The popped item is dispatched
    //    through the effect interpreter via execute_opcode (NOP/unrecognized
    //    opcodes are safe no-ops, so a value-init'd item still drains harmlessly).
    if (s.action_count > 0) {
        // CHOOSE-in-combat: a CHOOSE_CARD at the front that needs a
        // real player prompt (choice_requires_user) BLOCKS the pump -- leave it at
        // the head and hand control back to the player (the "hand card select
        // screen" is open). advance(CHOOSE, hand_slot) resolves one selection and
        // re-pumps. Forced / RANDOM choices auto-resolve via execute_opcode below.
        const ActionQueueItem& front = s.action_queue[s.action_head];
        if (static_cast<Opcode>(front.opcode) == Opcode::CHOOSE_CARD &&
            choice_requires_user(s, front)) {
            s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
            r.outcome = PumpOutcome::WAITING_ON_USER;
            return r;
        }
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
            // AbstractRoom.endTurn:397-405: the moment the turn ends,
            // every draw/discard/hand card's costForTurn resets to its cost --
            // SYNCHRONOUSLY, before any queued end-of-turn action resolves. Only
            // COST_MODIFIED_FOR_TURN rows (Infernal Blade's setCostForTurn(0)
            // attack) change; Blood for Blood's updateCost edits `cost` itself
            // and is untouched (reset_cost_for_turn's bit gate).
            for (uint8_t i = 0; i < s.draw_count; ++i) {
                reset_cost_for_turn(s, s.draw[i]);
            }
            for (uint8_t i = 0; i < s.discard_count; ++i) {
                reset_cost_for_turn(s, s.discard[i]);
            }
            for (uint8_t i = 0; i < s.hand_count; ++i) {
                reset_cost_for_turn(s, s.hand[i]);
            }
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
        start_of_turn(s, TurnStart::kSubsequentTurn);
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
